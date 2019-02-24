/**
 * @file rc_project_template.c
 *
 * This is meant to be a skeleton program for Robot Control projects. Change
 * this description and file name before modifying for your own purpose.
 */

#include <stdio.h>
#include <math.h>         // Math library required for 'sqrt'
#include <robotcontrol.h> // includes ALL Robot Control subsystems
#include <rc/mpu.h>
#include <rc/time.h>
#include <inttypes.h>    // for timing

// bus for Robotics Cape and BeagleboneBlue is 2
// change this for your platform
#define I2C_BUS 2

static int enable_magnetometer = 0;
static int enable_warnings = 0;

// System constants
//#define deltat 0.010f    // sampling period in seconds (shown as 10 ms)
#define gyroMeasError 3.14159265358979f * (5.0f / 180.0f) // gyro measurement error in rad/s (shown as 5 deg/s)
#define beta (float)sqrt(3.0f / 4.0f) * gyroMeasError // compute beta

// Global system variables
float a_x, a_y, a_z; // accelerometer measurements
float w_x, w_y, w_z; // gyroscope measurements in rad/s
float SEq_1 = 1.0f, SEq_2 = 0.0f, SEq_3 = 0.0f, SEq_4 = 0.0f; // estimated orientation quaternion elements with initial conditions

// function declarations
void filterUpdate(float w_x, float w_y, float w_z, float a_x, float a_y, float a_z, float *roll, float *pitch, float *yaw, double deltat);

// timing
uint64_t a,b,nanos;
double deltat = 0.010; // sampling period in seconds (initialized at 10 ms)

/**
 * This template contains these critical components
 * - ensure no existing instances are running and make new PID file
 * - start the signal handler
 * - initialize subsystems you wish to use
 * - while loop that checks for EXITING condition
 * - cleanup subsystems at the end
 *
 * @return     0 during normal operation, -1 on error
 */
int main()
{
	// make sure another instance isn't running
	// if return value is -3 then a background process is running with
	// higher privaledges and we couldn't kill it, in which case we should
	// not continue or there may be hardware conflicts. If it returned -4
	// then there was an invalid argument that needs to be fixed.
	if(rc_kill_existing_process(2.0)<-2) return -1;

	// start signal handler so we can exit cleanly
	if(rc_enable_signal_handler()==-1){
		fprintf(stderr,"ERROR: failed to start signal handler\n");
		return -1;
	}

	// make PID file to indicate your project is running
	// due to the check made on the call to rc_kill_existing_process() above
	// we can be fairly confident there is no PID file already and we can
	// make our own safely.
	rc_make_pid_file();

	rc_mpu_data_t data; //struct to hold new data

	// use defaults for now, except also enable magnetometer.
        rc_mpu_config_t conf = rc_mpu_default_config();
        conf.i2c_bus = I2C_BUS;
        conf.enable_magnetometer = enable_magnetometer;
        conf.show_warnings = enable_warnings;
        if(rc_mpu_initialize(&data, conf)){
                fprintf(stderr,"rc_mpu_initialize_failed\n");
                return -1;
        }

	float roll, pitch, yaw; // euler angle representation of orientation

	printf("   Accel XYZ(m/s^2)  |");
	printf("   Gyro XYZ (rad/s)  |");
	if(enable_magnetometer) printf("  Mag Field XYZ(uT)  |");
	printf("      R/P/Y (deg)    |");
	printf(" Ts (ms) |");
	printf("\n");

	int timing = 0;

	// run
	rc_set_state(RUNNING);
        while(rc_get_state()!=EXITING){
                if(rc_get_state()==RUNNING){
		printf("\r");

		//a = rc_nanos_since_epoch(); // start time

		// read sensor data
           	if(rc_mpu_read_accel(&data)<0){
                       	printf("read accel data failed\n");
               	}
               	if(rc_mpu_read_gyro(&data)<0){
                       	printf("read gyro data failed\n");
               	}

		a_x = data.accel[0];
		a_y = data.accel[1];
		a_z = data.accel[2];

		w_x = data.gyro[0]*DEG_TO_RAD;
		w_y = data.gyro[1]*DEG_TO_RAD;
		w_z = data.gyro[2]*DEG_TO_RAD;

		printf("%6.2f %6.2f %6.2f |", a_x, a_y, a_z);
		printf("%6.1f %6.1f %6.1f |", w_x, w_y, w_z);

		if(timing == 1) {
			b      = rc_nanos_since_epoch(); //end time
			nanos  = b - a;
			deltat = (double)nanos/1000000000;
		}

		filterUpdate(w_x, w_y, w_z, a_x, a_y, a_z, &roll, &pitch, &yaw, deltat);
		a = rc_nanos_since_epoch(); //start time
		if(timing == 0)
			timing = 1;

		roll  =  roll*(180.0f/3.14159265358979f);
		pitch = pitch*(180.0f/3.14159265358979f);
		yaw   =   yaw*(180.0f/3.14159265358979f);

		printf("%6.2f %6.2f %6.2f |", roll, pitch, yaw);

	        //b = rc_nanos_since_epoch(); //end time
		printf(" %" PRIu64 "ms  |", nanos/1000000);

		// always sleep at some point
        	rc_usleep(10000);
		}
	}

// close file descriptors
rc_remove_pid_file();	// remove pid file LAST
rc_mpu_power_off();
return 0;
}

/**
 * Update the 6DoF Madgwick Filter
 */
void filterUpdate(float w_x, float w_y, float w_z, float a_x, float a_y, float a_z, float *roll, float *pitch, float *yaw, double deltat)
{
// Local system variables
float norm; // vector norm
float SEqDot_omega_1, SEqDot_omega_2, SEqDot_omega_3, SEqDot_omega_4; // quaternion derivative from gyroscopeselements
float f_1, f_2, f_3; // objective function elements
float J_11or24, J_12or23, J_13or22, J_14or21, J_32, J_33; // objective function Jacobian elements
float SEqHatDot_1, SEqHatDot_2, SEqHatDot_3, SEqHatDot_4; // estimated direction of the gyroscope error
// Axulirary variables to avoid reapeated calcualtions
float halfSEq_1 = 0.5f * SEq_1;
float halfSEq_2 = 0.5f * SEq_2;
float halfSEq_3 = 0.5f * SEq_3;
float halfSEq_4 = 0.5f * SEq_4;
float twoSEq_1 = 2.0f * SEq_1;
float twoSEq_2 = 2.0f * SEq_2;
float twoSEq_3 = 2.0f * SEq_3;

// Normalise the accelerometer measurement
norm = (float)sqrt(a_x * a_x + a_y * a_y + a_z * a_z);
a_x /= norm;
a_y /= norm;
a_z /= norm;
// Compute the objective function and Jacobian
f_1 = twoSEq_2 * SEq_4 - twoSEq_1 * SEq_3 - a_x;
f_2 = twoSEq_1 * SEq_2 + twoSEq_3 * SEq_4 - a_y;
f_3 = 1.0f - twoSEq_2 * SEq_2 - twoSEq_3 * SEq_3 - a_z;
J_11or24 = twoSEq_3; // J_11 negated in matrix multiplication
J_12or23 = 2.0f * SEq_4;
J_13or22 = twoSEq_1; // J_12 negated in matrix multiplication
J_14or21 = twoSEq_2;
J_32 = 2.0f * J_14or21; // negated in matrix multiplication
J_33 = 2.0f * J_11or24; // negated in matrix multiplication
// Compute the gradient (matrix multiplication)
SEqHatDot_1 = J_14or21 * f_2 - J_11or24 * f_1;
SEqHatDot_2 = J_12or23 * f_1 + J_13or22 * f_2 - J_32 * f_3;
SEqHatDot_3 = J_12or23 * f_2 - J_33 * f_3 - J_13or22 * f_1;
SEqHatDot_4 = J_14or21 * f_1 + J_11or24 * f_2;
// Normalise the gradient
norm = (float)sqrt(SEqHatDot_1 * SEqHatDot_1 + SEqHatDot_2 * SEqHatDot_2 + SEqHatDot_3 * SEqHatDot_3 + SEqHatDot_4 * SEqHatDot_4);
SEqHatDot_1 /= norm;
SEqHatDot_2 /= norm;
SEqHatDot_3 /= norm;
SEqHatDot_4 /= norm;
// Compute the quaternion derrivative measured by gyroscopes
SEqDot_omega_1 = -halfSEq_2 * w_x - halfSEq_3 * w_y - halfSEq_4 * w_z;
SEqDot_omega_2 = halfSEq_1 * w_x + halfSEq_3 * w_z - halfSEq_4 * w_y;
SEqDot_omega_3 = halfSEq_1 * w_y - halfSEq_2 * w_z + halfSEq_4 * w_x;
SEqDot_omega_4 = halfSEq_1 * w_z + halfSEq_2 * w_y - halfSEq_3 * w_x;
// Compute then integrate the estimated quaternion derrivative
SEq_1 += (SEqDot_omega_1 - (beta * SEqHatDot_1)) * deltat;
SEq_2 += (SEqDot_omega_2 - (beta * SEqHatDot_2)) * deltat;
SEq_3 += (SEqDot_omega_3 - (beta * SEqHatDot_3)) * deltat;
SEq_4 += (SEqDot_omega_4 - (beta * SEqHatDot_4)) * deltat;
// Normalise quaternion
norm = (float)sqrt(SEq_1 * SEq_1 + SEq_2 * SEq_2 + SEq_3 * SEq_3 + SEq_4 * SEq_4);
SEq_1 /= norm;
SEq_2 /= norm;
SEq_3 /= norm;
SEq_4 /= norm;
// Convert to Euler Angles
*roll  = atan2(2*SEq_2*SEq_3 - 2*SEq_1*SEq_4, 2*SEq_1*SEq_1 + 2*SEq_2*SEq_2 - 1);
*pitch = -asin(2*SEq_2*SEq_4 + 2*SEq_1*SEq_3);
*yaw   = atan2(2*SEq_3*SEq_4 - 2*SEq_1*SEq_2, 2*SEq_1*SEq_1 + 2*SEq_4*SEq_4 - 1);
}
