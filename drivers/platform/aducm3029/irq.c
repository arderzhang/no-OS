/***************************************************************************//**
 *   @file   aducm3029/irq.c
 *   @brief  Implementation of External IRQ driver for ADuCM302x.
 *   @author Mihail Chindris (mihail.chindris@analog.com)
********************************************************************************
 * Copyright 2019(c) Analog Devices, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *  - The use of this software may or may not infringe the patent rights
 *    of one or more patent holders.  This license does not release you
 *    from the requirement that you obtain separate licenses from these
 *    patent holders to use this software.
 *  - Use of the software either in source or binary form, must be run
 *    on or directly connected to an Analog Devices Inc. component.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

/******************************************************************************/
/************************* Include Files **************************************/
/******************************************************************************/

#include "error.h"
#include <stdlib.h>
#include "irq_extra.h"
#include "irq.h"
#include "uart_extra.h"

/******************************************************************************/
/********************** Macros and Constants Definitions **********************/
/******************************************************************************/

/** The number of the first external interrupt, used by NVIC */
#define BASE_XINT_NB			(XINT_EVT0_IRQn)

/** Number of available external interrupts */
#define NB_EXT_INTERRUPTS		4u

/** Number of interrupts controllers available */
#define NB_INTERRUPT_CONTROLLERS	1u

/** Map the interrupt ID to the ADI_XINT_EVENT associated event */
static const uint32_t id_map_event[NB_INTERRUPTS] = {
	ADI_XINT_EVENT_INT0,	//ID ADUCM_EXTERNAL_INT0
	ADI_XINT_EVENT_INT1,	//ID ADUCM_EXTERNAL_INT1
	ADI_XINT_EVENT_INT2,	//ID ADUCM_EXTERNAL_INT2
	ADI_XINT_EVENT_INT3,	//ID ADUCM_EXTERNAL_INT3
	UART_EVT_IRQn		//UART IRQ

};

/******************************************************************************/
/***************************** Global Variables *******************************/
/******************************************************************************/

/**
 * Used to store the status of the driver. 1 if the driver is initialized, 0
 * otherwise
 */
static uint32_t		initialized;

/******************************************************************************/
/************************ Functions Definitions *******************************/
/******************************************************************************/

/**
 * @brief Initialized the controller for the ADuCM3029 external interrupts
 *
 * @param desc - Pointer where the configured instance is stored
 * @param param - Configuration information for the instance
 * @return \ref SUCCESS in case of success, \ref FAILURE otherwise.
 */
int32_t irq_ctrl_init(struct irq_ctrl_desc **desc,
		      const struct irq_init_param *param)
{
	struct aducm_irq_ctrl_desc		*aducm_desc;

	if (!desc || !param || initialized)
		return FAILURE;

	*desc = (struct irq_ctrl_desc *)calloc(1, sizeof(**desc));
	if (!*desc)
		return FAILURE;
	aducm_desc = (struct aducm_irq_ctrl_desc *)calloc(1,
			sizeof(*aducm_desc));
	if (!aducm_desc) {
		free(*desc);
		*desc = NULL;
		return FAILURE;
	}

	(*desc)->extra = aducm_desc;
	(*desc)->irq_ctrl_id = param->irq_ctrl_id;

	adi_xint_Init(aducm_desc->irq_memory, ADI_XINT_MEMORY_SIZE);

	initialized = 1;
	return SUCCESS;
}

/**
 * @brief Free the resources allocated by \ref irq_ctrl_init()
 * @param desc - Interrupt controller descriptor.
 * @return \ref SUCCESS in case of success, \ref FAILURE otherwise.
 */
int32_t irq_ctrl_remove(struct irq_ctrl_desc *desc)
{
	if (!desc || !desc->extra || !initialized)
		return FAILURE;

	/* Free external interrupts */
	for (uint32_t i; i < NB_EXT_INTERRUPTS; i++)
		adi_xint_DisableIRQ(id_map_event[i]);
	adi_xint_UnInit();

	/* Free UART */
	irq_unregister(desc, ADUCM_UART_INT_ID);
	free(desc->extra);
	free(desc);
	initialized = 0;

	return SUCCESS;
}

/**
 * @brief Registers a IRQ callback function to irq controller.
 * @param desc - The IRQ controller descriptor.
 * @param irq_id - Interrupt identifier.
 * @param callback - Function to be call when the irq occurs
 * @param context - Parameter to be passed to the callback
 * @param config - Configuration parameter for the irq
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t irq_register_callback(struct irq_ctrl_desc *desc,
			      uint32_t irq_id, IRQ_CALLBACK callback,
			      void *context, void *config)
{
	struct aducm_irq_ctrl_desc	*aducm_desc;

	if (!desc || !desc->extra || !initialized || !callback ||
	    irq_id >= NB_INTERRUPTS)
		return FAILURE;

	aducm_desc = desc->extra;

	if (irq_id < NB_EXT_INTERRUPTS) {
		aducm_desc->conf[irq_id].extern_trigger = (enum irq_mode)config;
		adi_xint_RegisterCallback(id_map_event[irq_id],
					  callback, context);
	} else { //if (irq_id == ADUCM_UART_INT_ID) {
		aducm_desc->conf[irq_id].uart_desc = (struct uart_desc *)config;
		if (!config)
			return FAILURE;
		if (SUCCESS != uart_register_callback(
			    aducm_desc->conf[irq_id].uart_desc,
			    callback, context))
			return FAILURE;
	}

	aducm_desc->callback_configured[irq_id] = true;
	return SUCCESS;
}

/**
 * @brief Unregister IRQ handling function for the specified <em>irq_id</em>.
 * @param desc - Interrupt controller descriptor.
 * @param irq_id - Id of the interrupt
 * @return \ref SUCCESS in case of success, \ref FAILURE otherwise.
 */
int32_t irq_unregister(struct irq_ctrl_desc *desc, uint32_t irq_id)
{
	struct aducm_irq_ctrl_desc	*aducm_desc;

	if (!desc || !desc->extra || !initialized ||
	    irq_id >= NB_INTERRUPTS)
		return FAILURE;

	aducm_desc = desc->extra;

	if (irq_id < NB_EXT_INTERRUPTS)
		adi_xint_RegisterCallback(id_map_event[irq_id], 0, 0);
	else if (aducm_desc->conf[irq_id].uart_desc) //irq_id = UART_ID
		if (SUCCESS != uart_register_callback(
			    aducm_desc->conf[irq_id].uart_desc, NULL,
			    NULL))
			return FAILURE;

	aducm_desc->conf[irq_id].uart_desc = 0;
	aducm_desc->conf[irq_id].extern_trigger = 0;
	aducm_desc->callback_configured[irq_id] = false;

	return SUCCESS;
}

/**
 * @brief Enable all previously enabled interrupts by \ref irq_source_enable().
 * @param desc - Interrupt controller descriptor.
 * @return \ref SUCCESS
 */
int32_t irq_global_enable(struct irq_ctrl_desc *desc)
{
	struct aducm_irq_ctrl_desc *aducm_desc;
	if (!desc || !desc->extra || !initialized)
		return FAILURE;

	aducm_desc = desc->extra;
	for (uint32_t i = 0; i < NB_EXT_INTERRUPTS; i++)
		if (aducm_desc->enabled & (1u << i))
			NVIC_EnableIRQ(BASE_XINT_NB + i);
	if (aducm_desc->enabled & (1u << ADUCM_UART_INT_ID))
		NVIC_EnableIRQ(id_map_event[ADUCM_UART_INT_ID]);

	return SUCCESS;
}

/**
 * @brief Disable all external interrupts
 * @param desc - Interrupt controller descriptor.
 * @return \ref SUCCESS
 */
int32_t irq_global_disable(struct irq_ctrl_desc *desc)
{
	struct aducm_irq_ctrl_desc *aducm_desc;
	if (!desc || !desc->extra || !initialized)
		return FAILURE;

	aducm_desc = desc->extra;
	for (uint32_t i = 0; i < NB_EXT_INTERRUPTS; i++)
		if (aducm_desc->enabled & (1u << i))
			NVIC_DisableIRQ(BASE_XINT_NB + i);
	if (aducm_desc->enabled & (1u << ADUCM_UART_INT_ID))
		NVIC_DisableIRQ(id_map_event[ADUCM_UART_INT_ID]);

	return SUCCESS;
}

/**
 * @brief Enable the interrupt
 *
 * In order for the <em>irq_id</em> interrupt to be triggered the GPIO
 * associated pin must be set as input.\n
 * The associated values are:\n
 * - ID 0 -> GPIO15
 * - ID 1 -> GPIO16
 * - ID 2 -> GPIO13
 * - ID 3 -> GPIO33
 *
 * @param desc - Interrupt controller descriptor.
 * @param irq_id - Id of the interrupt
 * @return \ref SUCCESS in case of success, \ref FAILURE otherwise.
 */
int32_t irq_source_enable(struct irq_ctrl_desc *desc, uint32_t irq_id)
{
	struct aducm_irq_ctrl_desc *aducm_desc;

	if (!desc || !desc->extra || !initialized ||
	    irq_id >= NB_INTERRUPTS)
		return FAILURE;
	aducm_desc = desc->extra;

	if (!aducm_desc->callback_configured[irq_id])
		return FAILURE;

	if (irq_id < NB_EXT_INTERRUPTS)
		adi_xint_EnableIRQ(id_map_event[irq_id],
				   aducm_desc->conf[irq_id].extern_trigger);
	else //if (irq_id == ADUCM_UART_INT_ID)
		NVIC_EnableIRQ(id_map_event[irq_id]);
	aducm_desc->enabled |= (1u << irq_id);

	return SUCCESS;
}

/**
 * @brief Disable the interrupt
 * @param desc - Interrupt controller descriptor.
 * @param irq_id - Id of the interrupt
 * @return \ref SUCCESS in case of success, \ref FAILURE otherwise.
 */
int32_t irq_source_disable(struct irq_ctrl_desc *desc, uint32_t irq_id)
{
	struct aducm_irq_ctrl_desc *aducm_desc;

	if (!desc || !desc->extra || !initialized ||
	    irq_id >= NB_INTERRUPTS)
		return FAILURE;

	aducm_desc = desc->extra;
	if (irq_id < NB_EXT_INTERRUPTS)
		adi_xint_DisableIRQ(id_map_event[irq_id]);
	else //if (irq_id == ADUCM_UART_INT_ID)
		NVIC_DisableIRQ(id_map_event[irq_id]);
	aducm_desc->enabled &= ~(1u << irq_id);

	return SUCCESS;
}
