// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include "config_cmd_task.h"


/**
 * Task routine to handle commands for registered handlers.
 *
 * @param  task The command task to process event notifications.  
 */
static void config_cmd_task_process_notification (struct config_cmd_task *task)
{
	uint32_t notification;
    uint8_t id;
    uint32_t action;

	do {
		xTaskNotifyWait (pdFALSE, ULONG_MAX, &notification, portMAX_DELAY);
        id = (uint8_t) ((notification & 0xff000000u) >> 24);
        action = (notification & 0x00ffffffu);
		task->handlers[id]->execute (task->handlers[id], action);		
	} while (1);
}

/**
 * Notify the command task context of an event by a command handler.
 *
 * @param task The command task to notify.
 * @param handler_id The identifier of the command handler.
 * @param action The action to process.
 *
 * @return 0 if notification was successful or an error code.
 */
int config_cmd_task_notify (struct config_cmd_task *task, uint8_t handler_id, uint32_t action)
{
	uint32_t notification = (((handler_id & 0x000000ffu) << 24) | action);

	if (task == NULL) {
		return CONFIG_CMD_TASK_INVALID_ARGUMENT;
	}

	xTaskNotify (task->task, notification, eSetBits);

	return 0;
}

/**
 * Initialize the command task context.
 * 
 * @param task The command task to initialize.
 * @param handler The list of command handlers to bind to the command task instance.
 * @param num_handlers The number of command handlers.
 *
 * @return 0 if the task was initialized or an error code
 */
int config_cmd_task_init (struct config_cmd_task *task, struct config_cmd_task_handler **handler,
	size_t num_handlers)
{
	int i;

	if ((task ==  NULL) || (handler == NULL)) {
		return CONFIG_CMD_TASK_INVALID_ARGUMENT;
	}

	memset (task, 0, sizeof (struct config_cmd_task));

	task->lock = xSemaphoreCreateMutex ();
	if (task->lock == NULL) {
		return CONFIG_CMD_TASK_NO_MEMORY;
	}

	task->num_handlers = num_handlers;
	task->handlers = handler;

	for (i = 0; i < num_handlers; i++) {
		handler[i]->bind (handler[i], task, (uint8_t) i);
	}

	return 0;
}

/**
 * Start running the command task context. No commands can be run until the command task has been
 * started.
 *
 * @param task The command task context to start.
 *
 * @return 0 if the task was started or an error code.
 */
int config_cmd_task_start (struct config_cmd_task *task)
{
	int status;

	if (task == NULL) {
		return CONFIG_CMD_TASK_INVALID_ARGUMENT;
	}

	status = xTaskCreate ((TaskFunction_t) config_cmd_task_process_notification, "config_cmd",
		6 * 256, task, CERBERUS_PRIORITY_NORMAL, &task->task);
	if (status != pdPASS) {
		task->task = NULL;
		return CONFIG_CMD_TASK_NO_MEMORY;
	}

	return 0;
}
