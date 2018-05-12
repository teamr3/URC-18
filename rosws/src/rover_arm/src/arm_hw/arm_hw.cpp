//
// Created by matthew on 2/15/18.
//

#include <rover_arm/arm_hw/arm_hw.h>
#include <boost/algorithm/string.hpp>
#include "motor_params.h"

namespace rover_arm {
    uint16_t convertEffToMotorV(double value) {
        double effort_normalized = value / MOTOR_EXERTED_EFFORT;
        double offset = MOTOR_OFFSET * effort_normalized;

        // todo: update for different joints

        return (uint16_t)(MOTOR_MID + offset);
    }

    double convertToPositionOffsetRotate(int ticks) {
        return (ticks / TICKS_PER_REVOLUTION) * (3.1415926 * 2);
    }
}

void rover_arm::ArmHW::init(hardware_interface::RobotHW *hw) {
    ros::NodeHandle nh("/");

    if (!this->device.isDisconnected()) {
        this->setupDeviceOnConnect();
    }

    this->lastReconnectAttemptTime = ros::Time::now();

    hardware_interface::ActuatorStateHandle jsh_io("arm_xy_inner_motor", &pos[0], &eff[0], &vel[0]);
    hardware_interface::ActuatorStateHandle jsh_su("arm_xy_outer_motor", &pos[1], &eff[1], &vel[1]);
    hardware_interface::ActuatorStateHandle jsh_sp("arm_slide_motor", &pos[2], &eff[2], &vel[2]);
    hardware_interface::ActuatorStateHandle jsh_tl("arm_spin_inner_motor", &pos[3], &eff[3], &vel[3]);

    this->act_state_interface.registerHandle(jsh_io);
    this->act_state_interface.registerHandle(jsh_su);
    this->act_state_interface.registerHandle(jsh_sp);
    this->act_state_interface.registerHandle(jsh_tl);

    hardware_interface::ActuatorHandle jh_io(jsh_io, &cmd[0]);
    hardware_interface::ActuatorHandle jh_su(jsh_su, &cmd[1]);
    hardware_interface::ActuatorHandle jh_sp(jsh_sp, &cmd[2]);
    hardware_interface::ActuatorHandle jh_tl(jsh_tl, &cmd[3]);

    this->act_eff_interface.registerHandle(jh_io);
    this->act_eff_interface.registerHandle(jh_su);
    this->act_eff_interface.registerHandle(jh_sp);
    this->act_eff_interface.registerHandle(jh_tl);

    diag_dhd.hardwareID = "due-arm";
    diag_dhd.data["connected"] = !this->device.isDisconnected() ? "yes" : "no";
    diag_dhd.data["device"] = "/dev/i2c-" + std::to_string(ARDUINO_ARM_BUS);
    diag_dhd.data["address"] = std::to_string(ARDUINO_ARM_ADDRESS);
    controller_diagnostics::DiagnosticHandle dH("arm_arduino", &diag_dhd);

    diag_interface.registerHandle(dH);

    hw->registerInterface(&act_eff_interface);
    hw->registerInterface(&act_state_interface);
    hw->registerInterface(&diag_interface);

    std::string robot_description;
    if (!nh.getParam("robot_description", robot_description)) {
        ROS_FATAL_STREAM("Failed to load robot_description, this node will probably now crash");
    }

    this->transmission_loader_ = new transmission_interface::TransmissionInterfaceLoader(hw, &this->robot_transmissions);
    transmission_interface::TransmissionParser parser;
    std::vector<transmission_interface::TransmissionInfo> infos;
    parser.parse(robot_description, infos);
    for (auto e : infos) {
        if (boost::starts_with(e.name_, "arm")) {
            if (!this->transmission_loader_->load(e)) {
                ROS_FATAL_STREAM("ASDF");
            }
        }
    }

}

void rover_arm::ArmHW::write() {
    this->robot_transmissions.get<transmission_interface::JointToActuatorEffortInterface>()->propagate();
    //ROS_INFO_STREAM("a " << cmd[0]);
    if (!this->device.isDisconnected()) {
        this->device.writeMicroseconds(MOTOR_INNEROUTR, convertEffToMotorV(cmd[0]));
        this->device.writeMicroseconds(MOTOR_SLIDEUNIT, convertEffToMotorV(cmd[1]));
        this->device.writeMicroseconds(MOTOR_SLIDEPOLE, convertEffToMotorV(cmd[2]));
        this->device.writeMicroseconds(MOTOR_GRIPPTILT, convertEffToMotorV(cmd[3]));
    }
}

void rover_arm::ArmHW::read() {
    this->robot_transmissions.get<transmission_interface::ActuatorToJointStateInterface>()->propagate();
    if (this->device.isDisconnected()) {
        diag_dhd.data["connected"] = "no";
        diag_dhd.status = diagnostic_msgs::DiagnosticStatus::ERROR;
        diag_dhd.message = "The arduino is not connected or is not listening to i2c";

        if ((ros::Time::now() - this->lastReconnectAttemptTime).toSec() > 3.0) {
            this->lastReconnectAttemptTime = ros::Time::now();
            if (this->device.tryOpen()) {
                this->setupDeviceOnConnect();
                ROS_INFO_STREAM("Connected!");
            }
            else {
                ROS_WARN_STREAM("Failed to connect to I2C device, retry in 3 seconds");
            }
        }
    }
    else {
        diag_dhd.data["connected"] = "yes";
        diag_dhd.status = diagnostic_msgs::DiagnosticStatus::OK;
        diag_dhd.message = "The arduino is connected and responding to i2c";

        int ticks_inneroutr = this->jointEncoders[0].encoderValue();
        int ticks_slideunit = this->jointEncoders[1].encoderValue();
        int ticks_slidepole = this->jointEncoders[2].encoderValue();
        int ticks_gripptilt = this->jointEncoders[3].encoderValue();

        pos[0] = convertToPositionOffsetRotate(ticks_inneroutr); // todo: get gear crappo for this
        pos[1] = convertToPositionOffsetRotate(ticks_slideunit);
        pos[2] = 0; // todo
        pos[3] = convertToPositionOffsetRotate(ticks_gripptilt);
    }
}

void rover_arm::ArmHW::setupDeviceOnConnect() {
    this->device.openPinAsMotor(MOTOR_INNEROUTR);
    this->device.openPinAsMotor(MOTOR_SLIDEUNIT);
    this->device.openPinAsMotor(MOTOR_SLIDEPOLE);
    this->device.openPinAsMotor(MOTOR_GRIPPTILT);

    this->jointEncoders[0] = this->device.openPinAsEncoder(ENCODER_INNEROUTR_A, ENCODER_INNEROUTR_B);
    this->jointEncoders[1] = this->device.openPinAsEncoder(ENCODER_SLIDEUNIT_A, ENCODER_SLIDEUNIT_B);
    this->jointEncoders[2] = this->device.openPinAsEncoder(ENCODER_SLIDEPOLE_A, ENCODER_SLIDEPOLE_B);
    this->jointEncoders[3] = this->device.openPinAsEncoder(ENCODER_GRIPPTILT_A, ENCODER_GRIPPTILT_B);
}
