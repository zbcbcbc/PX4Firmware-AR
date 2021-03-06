/*************************************************************
 * Copyright @ Dullerud's Autonomous Lab
 * University of Illinois Urbana Champaign
 * 
 * Author @ Bicheng Zhang <viczhang1990@gmail.com>
 * 
 * <-----------------Drone Software------------------------>
 *
 *
 *            ********                  *****
 *             -A---               --A---
 *                
 *                      U U
 *                      ^
 *
 *
 *
 ************************************************************/

/*
 * @file att_control_pid.c
 *
 * Implementation of attitude controller for multirotors.
 *
 * @author Thomas Gubler <thomasgubler@student.ethz.ch>
 * @author Julian Oes <joes@student.ethz.ch>
 * @author Laurens Mackay <mackayl@student.ethz.ch>
 * @author Tobias Naegeli <naegelit@student.ethz.ch>
 * @author Martin Rutschmann <rutmarti@student.ethz.ch>
 * @author Lorenz Meier <lm@inf.ethz.ch>
 *
 * @Developer Bicheng Zhang <zhang368@illinois.edu>
 */

#include "att_control_pid.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <float.h>
#include <math.h>
#include <systemlib/pid/pid.h>
#include <systemlib/param/param.h>
#include <drivers/drv_hrt.h>

PARAM_DEFINE_FLOAT(AC_PID_YAWPOS_P, 2.0f);
PARAM_DEFINE_FLOAT(AC_PID_YAWPOS_I, 0.15f);
PARAM_DEFINE_FLOAT(AC_PID_YAWPOS_D, 0.0f);
//PARAM_DEFINE_FLOAT(MC_YAWPOS_AWU, 1.0f);
//PARAM_DEFINE_FLOAT(MC_YAWPOS_LIM, 3.0f);

PARAM_DEFINE_FLOAT(AC_PID_ATT_P, 6.8f);
PARAM_DEFINE_FLOAT(AC_PID_ATT_I, 0.0f);
PARAM_DEFINE_FLOAT(AC_PID_ATT_D, 0.0f);
//PARAM_DEFINE_FLOAT(MC_ATT_AWU, 0.05f);
//PARAM_DEFINE_FLOAT(MC_ATT_LIM, 0.4f);

//PARAM_DEFINE_FLOAT(MC_ATT_XOFF, 0.0f);
//PARAM_DEFINE_FLOAT(MC_ATT_YOFF, 0.0f);

struct att_control_pid_params {
	float yaw_p;
	float yaw_i;
	float yaw_d;
	//float yaw_awu;
	//float yaw_lim;

	float att_p;
	float att_i;
	float att_d;
	//float att_awu;
	//float att_lim;

	//float att_xoff;
	//float att_yoff;
};

struct att_control_pid_param_handles {
	param_t yaw_p;
	param_t yaw_i;
	param_t yaw_d;
	//param_t yaw_awu;
	//param_t yaw_lim;

	param_t att_p;
	param_t att_i;
	param_t att_d;
	//param_t att_awu;
	//param_t att_lim;

	//param_t att_xoff;
	//param_t att_yoff;
};

/**
 * Initialize all parameter handles and values
 *
 */
static int parameters_init(struct att_control_pid_param_handles *h);

/**
 * Update all parameters
 *
 */
static int parameters_update(const struct att_control_pid_param_handles *h, struct att_control_pid_params *p);


static int parameters_init(struct att_control_pid_param_handles *h)
{
	/* PID parameters */
	h->yaw_p 	=	param_find("AC_PID_YAWPOS_P");
	h->yaw_i 	=	param_find("AC_PID_YAWPOS_I");
	h->yaw_d 	=	param_find("AC_PID_YAWPOS_D");
	//h->yaw_awu 	=	param_find("MC_YAWPOS_AWU");
	//h->yaw_lim 	=	param_find("MC_YAWPOS_LIM");

	h->att_p 	= 	param_find("AC_PID_ATT_P");
	h->att_i 	= 	param_find("AC_PID_ATT_I");
	h->att_d 	= 	param_find("AC_PID_ATT_D");
	//h->att_awu 	= 	param_find("MC_ATT_AWU");
	//h->att_lim 	= 	param_find("MC_ATT_LIM");

	//h->att_xoff 	= 	param_find("MC_ATT_XOFF");
	//h->att_yoff 	= 	param_find("MC_ATT_YOFF");

	return OK;
}

static int parameters_update(const struct att_control_pid_param_handles *h, struct att_control_pid_params *p)
{
	param_get(h->yaw_p, &(p->yaw_p));
	param_get(h->yaw_i, &(p->yaw_i));
	param_get(h->yaw_d, &(p->yaw_d));
	//param_get(h->yaw_awu, &(p->yaw_awu));
	//param_get(h->yaw_lim, &(p->yaw_lim));

	param_get(h->att_p, &(p->att_p));
	param_get(h->att_i, &(p->att_i));
	param_get(h->att_d, &(p->att_d));
	//param_get(h->att_awu, &(p->att_awu));
	//param_get(h->att_lim, &(p->att_lim));

	//param_get(h->att_xoff, &(p->att_xoff));
	//param_get(h->att_yoff, &(p->att_yoff));

	return OK;
}

void att_control_pid(const struct vehicle_attitude_setpoint_s *att_sp,
				 const struct vehicle_attitude_s *att, struct vehicle_rates_setpoint_s *rates_sp, bool control_yaw_position, bool reset_integral)
{
	static uint64_t last_run = 0;
	static uint64_t last_input = 0;
	float deltaT = (hrt_absolute_time() - last_run) / 1000000.0f;
	last_run = hrt_absolute_time();

	if (last_input != att_sp->timestamp) {
		last_input = att_sp->timestamp;
	}

	static int motor_skip_counter = 0;

	static PID_t pitch_controller;
	static PID_t roll_controller;

	static struct att_control_pid_params p;
	static struct att_control_pid_param_handles h;

	static bool initialized = false;

	static float yaw_error;

	/* initialize the pid controllers when the function is called for the first time */
	if (initialized == false) {
		parameters_init(&h);
		parameters_update(&h, &p);

		pid_init(&pitch_controller, p.att_p, p.att_i, p.att_d, 1000.0f, 1000.0f, PID_MODE_DERIVATIV_SET, 0.0f);
		pid_init(&roll_controller, p.att_p, p.att_i, p.att_d, 1000.0f, 1000.0f, PID_MODE_DERIVATIV_SET, 0.0f);

		initialized = true;
	}

	/* load new parameters with lower rate */
	if (motor_skip_counter % 500 == 0) {
		/* update parameters from storage */
		parameters_update(&h, &p);

		/* apply parameters */
		pid_set_parameters(&pitch_controller, p.att_p, p.att_i, p.att_d, 1000.0f, 1000.0f);
		pid_set_parameters(&roll_controller, p.att_p, p.att_i, p.att_d, 1000.0f, 1000.0f);
	}

	/* reset integrals if needed */
	if (reset_integral) {
		pid_reset_integral(&pitch_controller);
		pid_reset_integral(&roll_controller);
		//TODO pid_reset_integral(&yaw_controller);
	}

	/* calculate current control outputs */

	/* control pitch (forward) output */
	rates_sp->pitch = pid_calculate(&pitch_controller, att_sp->pitch_body ,
					att->pitch, att->pitchspeed, deltaT);

	/* control roll (left/right) output */
	rates_sp->roll = pid_calculate(&roll_controller, att_sp->roll_body ,
				       att->roll, att->rollspeed, deltaT);

	if (control_yaw_position) {
		/* control yaw rate */
		// TODO use pid lib

		/* positive error: rotate to right, negative error, rotate to left (NED frame) */
		// yaw_error = _wrap_pi(att_sp->yaw_body - att->yaw);

		yaw_error = att_sp->yaw_body - att->yaw;

		if (yaw_error > M_PI_F) {
			yaw_error -= M_TWOPI_F;

		} else if (yaw_error < -M_PI_F) {
			yaw_error += M_TWOPI_F;
		}

		rates_sp->yaw = p.yaw_p * (yaw_error) - (p.yaw_d * att->yawspeed);
	}

	rates_sp->thrust = att_sp->thrust;
    //need to update the timestamp now that we've touched rates_sp
    rates_sp->timestamp = hrt_absolute_time();

	motor_skip_counter++;
}
