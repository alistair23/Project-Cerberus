// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "host_processor_dual_full_bypass.h"
#include "host_state_manager.h"


static int host_processor_dual_full_bypass_enable_bypass_mode (struct host_processor_dual *host)
{
	return host->filter->set_bypass_mode (host->filter,
		(host_state_manager_get_read_only_flash (host->state) == SPI_FILTER_CS_0) ?
			SPI_FILTER_BYPASS_CS0 : SPI_FILTER_BYPASS_CS1);
}

/**
 * Initialize the interface for executing host processor actions.  Unprotected flash will be
 * accessible in full bypass mode.
 *
 * @param host The host processor instance to initialize.
 * @param control The interface for controlling the host processor.
 * @param flash The manager for the flash devices for the host processor.
 * @param state The state information for the host.
 * @param filter The SPI filter controlling flash access for the host processor.
 * @param pfm The manager for PFMs for the host processor.
 * @param recovery The recovery image manager for the host processor.
 *
 * @return 0 if the host processor interface was successfully initialized or an error code.
 */
int host_processor_dual_full_bypass_init (struct host_processor_dual_full_bypass *host,
	struct host_control *control, struct host_flash_manager *flash, struct state_manager *state,
	struct spi_filter_interface *filter, struct pfm_manager *pfm,
	struct recovery_image_manager *recovery)
{
	int status = host_processor_dual_init_internal (&host->base, control, flash, state, filter,
		pfm, recovery);
	if (status != 0) {
		return status;
	}

	host->base.internal.enable_bypass_mode = host_processor_dual_full_bypass_enable_bypass_mode;

	return 0;
}

/**
 * Initialize the interface for executing host processor actions.  Unprotected flash will be
 * accessible in full bypass mode.
 *
 * While host flash is being accessed, the host processor will not be held in reset.  After the host
 * flash accesses have been completed, the host processor reset will be pulsed for 100ms.
 *
 * @param host The host processor instance to initialize.
 * @param control The interface for controlling the host processor.
 * @param flash The manager for the flash devices for the host processor.
 * @param state The state information for the host.
 * @param filter The SPI filter controlling flash access for the host processor.
 * @param pfm The manager for PFMs for the host processor.
 * @param recovery The recovery image manager for the host processor.
 *
 * @return 0 if the host processor interface was successfully initialized or an error code.
 */
int host_processor_dual_full_bypass_init_pulse_reset (struct host_processor_dual_full_bypass *host,
	struct host_control *control, struct host_flash_manager *flash, struct state_manager *state,
	struct spi_filter_interface *filter, struct pfm_manager *pfm,
	struct recovery_image_manager *recovery)
{
	int status = host_processor_dual_init_pulse_reset_internal (&host->base, control, flash, state,
		filter, pfm, recovery);
	if (status != 0) {
		return status;
	}

	host->base.internal.enable_bypass_mode = host_processor_dual_full_bypass_enable_bypass_mode;

	return 0;
}

/**
 * Release the resources used by the host processor interface.
 *
 * @param host The host processor instance to release.
 */
void host_processor_dual_full_bypass_release (struct host_processor_dual_full_bypass *host)
{
	if (host) {
		host_processor_dual_release (&host->base);
	}
}
