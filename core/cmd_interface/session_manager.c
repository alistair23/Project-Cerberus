// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "crypto/aes.h"
#include "crypto/hash.h"
#include "riot/riot_key_manager.h"
#include "session_manager.h"


/**
 * Search session manager's active sessions table and find entry for requested EID if it exists. 
 *
 * @param session Session manager instance to utilize.
 * @param eid Requested EID for device in session. 
 *
 * @return Requested session container if exists, NULL otherwise.
 */
struct session_manager_entry* session_manager_get_session (struct session_manager *session, 
	uint8_t eid)
{
	struct session_manager_entry *curr_entry;
	size_t i_session;

	for (i_session = 0; i_session < session->num_sessions; ++i_session) {
		curr_entry = (struct session_manager_entry*) 
			&session->sessions_table[i_session * session->entry_len];

		if ((curr_entry->eid == eid) && 
			(curr_entry->session_state != SESSION_STATE_UNUSED)) {
			return curr_entry;
		}
	}

	return NULL;
}

/**
 * Search session manager's sessions table and find first unused entry. 
 *
 * @param session Session manager instance to utilize.
 *
 * @return Free session container if exists, NULL otherwise.
 */
struct session_manager_entry* session_manager_get_free_session (struct session_manager *session)
{
	struct session_manager_entry *curr_entry;
	size_t i_session;

	for (i_session = 0; i_session < session->num_sessions; ++i_session) {
		curr_entry = (struct session_manager_entry*) 
			&session->sessions_table[i_session * session->entry_len];

		if (curr_entry->session_state == SESSION_STATE_UNUSED) {
			return curr_entry;
		}
	}

	return NULL;
}

/**
 * Find AES session key for requested EID then set it in the AES engine
 *
 * @param session Session manager instance to utilize.
 * @param eid Device EID.
 *
 * @return Completion status, 0 if success or an error code.
 */
static int session_manager_set_key (struct session_manager *session, uint8_t eid)
{
	struct session_manager_entry *curr_session;
	int status;

	curr_session = session_manager_get_session (session, eid);
	if (curr_session == NULL) {
		return SESSION_MANAGER_UNEXPECTED_EID;
	}
	else if (curr_session->session_state != SESSION_STATE_ESTABLISHED) {
		return SESSION_MANAGER_SESSION_NOT_ESTABLISHED;
	}

	status = session->aes->set_key (session->aes, curr_session->aes_key, 
		sizeof (curr_session->aes_key));
	
	return status;
}

/**
 * Check if device EID is on an established session. 
 *
 * @param session Session manager instance to utilize.
 * @param eid Device EID.  
 *
 * @return 1 if established, 0 if not or an error code.
 */
int session_manager_is_session_established (struct session_manager *session, uint8_t eid)
{
	struct session_manager_entry *req_session;

	if (session == NULL) {
		return SESSION_MANAGER_INVALID_ARGUMENT;
	}

	req_session = session_manager_get_session (session, eid);
	if (req_session == NULL) {
		return SESSION_MANAGER_UNEXPECTED_EID;
	}

	return (req_session->session_state == SESSION_STATE_ESTABLISHED);
}

/**
 * Decrypt message using AES session key generated for session with device with requested EID.
 *
 * @param session Session manager instance to utilize.
 * @param request Request to encrypt.
 *
 * @return Completion status, 0 if success or an error code.
 */
int session_manager_decrypt_message (struct session_manager *session, 
	struct cmd_interface_request *request)
{
	uint8_t *payload;
	size_t payload_len;
	size_t buffer_len;
	int status;

	if ((session == NULL) || (request == NULL)) {
		return SESSION_MANAGER_INVALID_ARGUMENT;
	}

	if (request->length <= (SESSION_MANAGER_TRAILER_LEN + 
		sizeof (struct cerberus_protocol_header))) {
		return SESSION_MANAGER_MALFORMED_MSG;
	}

	if ((sizeof (request->data) < (request->length - SESSION_MANAGER_TRAILER_LEN)) || 
		(request->max_response <= (sizeof (struct cerberus_protocol_header) + 
		SESSION_MANAGER_TRAILER_LEN))) {
		return SESSION_MANAGER_BUF_TOO_SMALL;
	}

	payload = request->data + sizeof (struct cerberus_protocol_header);
	payload_len = request->length - sizeof (struct cerberus_protocol_header) - 
		SESSION_MANAGER_TRAILER_LEN;
	buffer_len = sizeof (request->data) - sizeof (struct cerberus_protocol_header);

	status = session_manager_set_key (session, request->source_eid);
	if (status != 0) {
		return status;
	}
	
	request->length -= SESSION_MANAGER_TRAILER_LEN;

	return session->aes->decrypt_data (session->aes, payload, payload_len, &payload[payload_len], 
		&payload[payload_len + CERBERUS_PROTOCOL_AES_GCM_TAG_LEN], CERBERUS_PROTOCOL_AES_IV_LEN, 
		payload, buffer_len);
}

/**
 * Encrypt message using AES session key generated for session with device with requested EID.
 *
 * @param session Session manager instance to utilize.
 * @param request Request to encrypt.
 *
 * @return Completion status, 0 if success or an error code.
 */
int session_manager_encrypt_message (struct session_manager *session, 
	struct cmd_interface_request *request)
{
	struct cerberus_protocol_header *header;
	uint8_t *aes_iv;
	uint8_t *payload;
	size_t payload_len;
	size_t buffer_len;
	int status;

	if ((session == NULL) || (request == NULL)) {
		return SESSION_MANAGER_INVALID_ARGUMENT;
	} 

	if (request->length <= sizeof (struct cerberus_protocol_header)) {
		return 0;
	}
		
	if ((request->length + SESSION_MANAGER_TRAILER_LEN) > request->max_response) {
		return SESSION_MANAGER_BUF_TOO_SMALL;
	}
	
	payload = request->data + sizeof (struct cerberus_protocol_header);
	payload_len = request->length - sizeof (struct cerberus_protocol_header);
	buffer_len = request->max_response - sizeof (struct cerberus_protocol_header);
	aes_iv = &payload[payload_len + CERBERUS_PROTOCOL_AES_GCM_TAG_LEN];

	status = session_manager_set_key (session, request->source_eid);
	if (status != 0) {
		return status;
	}

	status = session->rng->generate_random_buffer (session->rng, CERBERUS_PROTOCOL_AES_IV_LEN, 
		aes_iv);
	if (status != 0) {
		return status;
	}

	status = session->aes->encrypt_data (session->aes, payload, payload_len, aes_iv, 
		CERBERUS_PROTOCOL_AES_IV_LEN, payload, buffer_len - SESSION_MANAGER_TRAILER_LEN, 
		&payload[payload_len], 
		CERBERUS_PROTOCOL_AES_GCM_TAG_LEN);
	if (status != 0) {
		return status;
	}

	request->length += SESSION_MANAGER_TRAILER_LEN;

	header = (struct cerberus_protocol_header*) request->data;
	header->crypt = 1;

	return status;
}

/**
 * Use provided nonces to either create or restart session with device specified using EID.
 *
 * @param session Session manager instance to utilize.
 * @param eid Device EID.
 * @param device_nonce 32 byte random nonce generated by device used for AES key generation.
 * @param cerberus_nonce 32 byte random nonce generated by Cerberus used for AES key generation.
 *
 * @return Completion status, 0 if success or an error code.
 */
int session_manager_add_session (struct session_manager *session, uint8_t eid, 
	const uint8_t *device_nonce, const uint8_t *cerberus_nonce)
{
	struct session_manager_entry *curr_session;

	if ((session == NULL) || (device_nonce == NULL) || (cerberus_nonce == NULL)) {
		return SESSION_MANAGER_INVALID_ARGUMENT;
	}

	curr_session = session_manager_get_session (session, eid);
	if (curr_session == NULL) {
		curr_session = session_manager_get_free_session (session);
		if (curr_session == NULL) {
			return SESSION_MANAGER_FULL;
		}
	}

	memcpy (curr_session->device_nonce, device_nonce, SESSION_MANAGER_NONCE_LEN);
	memcpy (curr_session->cerberus_nonce, cerberus_nonce, SESSION_MANAGER_NONCE_LEN);
	curr_session->session_state = SESSION_STATE_SETUP;
	curr_session->eid = eid;

	return 0;
}

/**
 * Initialize session manager instance and use the provided session manager entries table
 *
 * @param session Session manager instance to initialize.
 * @param aes AES engine to utilize for packet encryption/decryption. 
 * @param hash Hash engine to utilize for AES key generation. 
 * @param rng RNG engine used to generate IV buffers.
 * @param riot RIoT key manager to utilize to get alias key for AES key generation.
 * @param sessions_table Table to use to store session manager entries. 
 * @param num_sessions Number of sessions to support.
 * @param entry_len Length of a session table entry. 
 * @param sessions_table_preallocated Flag indicating whether sessions table was statically 
 * 	allocated.
 *
 * @return Initialization status, 0 if success or an error code.
 */
int session_manager_init (struct session_manager *session, struct aes_engine *aes, 
	struct hash_engine *hash, struct rng_engine *rng, struct riot_key_manager *riot, 
	uint8_t *sessions_table, size_t num_sessions, size_t entry_len, 
	bool sessions_table_preallocated)
{
	if ((session == NULL) || (aes == NULL) || (hash == NULL) || (rng == NULL) || (riot == NULL) || 
		(sessions_table == NULL)) {
		return SESSION_MANAGER_INVALID_ARGUMENT;
	}

	memset (session, 0, sizeof (struct session_manager));
	memset (sessions_table, 0, entry_len * num_sessions);

	session->aes = aes;
	session->hash = hash;
	session->rng = rng;
	session->riot = riot;
	session->num_sessions = num_sessions;
	session->sessions_table = sessions_table;
	session->entry_len = entry_len;
	session->sessions_table_preallocated = sessions_table_preallocated;

	return 0;
}

/**
 * Release session manager
 *
 * @param session Session manager instance to release
 */
void session_manager_release (struct session_manager *session)
{
	if ((session != NULL) && !session->sessions_table_preallocated) {
		platform_free (session->sessions_table);
	}
}
