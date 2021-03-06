// C/C++ Headers
#include <stdlib.h>
#include <cmath>
#include <string>
#include <algorithm>

// Utility library headers
#include <boost/algorithm/string.hpp>

// ROS Headers
#include <ros/ros.h>
#include <std_msgs/Float32.h>
#include <sensor_msgs/JointState.h>
#include <std_srvs/Empty.h>
#include <std_srvs/Trigger.h>
#include <std_srvs/SetBool.h>


using std::string;


ros::Publisher pub_setpoint, pub_goal;

static double speed = 600 / 200 * 20 * 2;
static double x, y;
static double x_col, y_col;
static double x_goal, y_goal;
static double x_bound, y_bound;
static bool led_on, motors_on, endeff_down;
static double v_scale;
static bool reported = true;

// Relative movement subscriber callback
void move(const sensor_msgs::JointState &msg) {
  x_goal += msg.position[0];
  y_goal += msg.position[1];
  reported = false;
}


// Absolute movement subscriber callback
void move_to(const sensor_msgs::JointState &msg) {
  x_goal = msg.position[0];
  y_goal = msg.position[1];
  reported = false;
}


// Scale maximum velocity of device callback
void velocity_scale(const std_msgs::Float32 &msg) {
  float scale = std::min(std::max(msg.data, 0.0f), 1.0f);
  v_scale = scale;
}


// Halt service handler
bool halt(std_srvs::Empty::Request &req, std_srvs::Empty::Response &resp) {
  x_goal = x;
  y_goal = y;

  return true;
}


// End effector tap action service handler
bool tap(std_srvs::Empty::Request &req, std_srvs::Empty::Response &resp) {
  endeff_down = true;
  ros::Duration(0.5).sleep();
  endeff_down = false;

  return true;
}


// Homing sequence service handler
bool home(std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &resp) {
  if (motors_on) {
    ros::Rate r(60);

    while (x > x_bound) {
      x -= speed * v_scale * 0.2;
      r.sleep();
    }
    x = x_bound;
    while (x < 0) {
      x += speed * v_scale * 0.2;
      r.sleep();
    }
    x = 0;

    while (x > -80) {
      x -= speed * v_scale * 0.2;
      r.sleep();
    }
    x = -80;

    while (y < y_bound) {
      y += speed * v_scale * 0.2;
      r.sleep();
    }
    y = y_bound;
    while (y > 0) {
      y -= speed * v_scale * 0.2;
      r.sleep();
    }
    y = 0;

    x_goal = 0;
    y_goal = 0;
    reported = false;
    
    resp.success = true;
    resp.message = "Successfully homed.";
  } else {
    resp.success = false;
    resp.message = "Failed to home.";
  }
  

  return true;
}


// End effector state set service handler
bool set_endeff(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &resp) {
  if (req.data) {
    endeff_down = true;
    resp.message = "End Effector Pressed";
  } else {
    endeff_down = false;
    resp.message = "End Effector Released";
  }

  resp.success = true;

  return true;
}


// LED state set service handler
bool set_led(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &resp) {
  if (req.data) {
    led_on = true;
    resp.message = "LED Lit";
  } else {
    led_on = false;
    resp.message = "LED Off";
  }

  resp.success = true;
  
  return true;
}


// Motor state set service handler
bool set_motors(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &resp) {
  if (req.data) {
    motors_on = true;
    resp.message = "Motors Enabled";
  } else {
    motors_on = false;
    resp.message = "Motors Disabled";
  }

  resp.success = true;
  
  return true;
}


// Position publisher timer event
void publishPosition(const ros::TimerEvent &e) {
  unsigned static int s = 0;

  
    sensor_msgs::JointState msg;

    // Assign message data
    msg.header.seq = s;
    msg.header.stamp = ros::Time::now();
    msg.header.frame_id = "rvc";

    msg.name.resize(2);
    msg.name[0] = "x";
    msg.name[1] = "y";

    msg.position.resize(2);
    msg.position[0] = x;
    msg.position[1] = y;

    msg.effort.resize(2);
    msg.effort[0] = x_col;
    msg.effort[1] = y_col;
    
    // Publish and increment sequence counter
    pub_setpoint.publish(msg);
    // Also publish to goal_js if message indicates end of motion 
    if (x_goal == x && y_goal == y && !reported) {
      pub_goal.publish(msg);
      reported = true;
    }
    s++;
  
}


void simulateMotors(const ros::TimerEvent &e) {
  double y_vel, x_vel;
  if (y_goal - y == 0 && x_goal - x == 0) {
    x_vel = 0;
    y_vel = 0;
  } else if (fabs(y_goal - y) > fabs(x_goal - x)) {
    y_vel = speed * v_scale * (y_goal - y) / fabs(y_goal - y);
    x_vel = (x_goal - x) / fabs(y_goal - y) * speed * v_scale;
  } else {
    x_vel = speed * v_scale * (x_goal - x) / fabs(x_goal - x);
    y_vel = (y_goal - y) / fabs(x_goal - x) * speed * v_scale;
  }

  x += x_vel * 0.02;
  y += y_vel * 0.02;

  if (fabs(x_goal - x) <= 2.4)
    x = x_goal;
  if (fabs(y_goal - y) <= 2.4)
    y = y_goal;

}


int main(int argc, char **argv) {
  // Initialize ROS
  ros::init(argc, argv, "rvc_ros_simulator");
  ros::NodeHandle nh;

  x_bound = -580;
  y_bound = 300;
  v_scale = 1.0;

  // Open serial port to device
  string port = "/dev/ttyUSB0";
  
  // Check that port is open, exit if not
  if (true)
    ROS_INFO("Opened serial port %s", port.c_str());
  else {
    ROS_FATAL("Failed to open serial port %s", port.c_str());
    ros::shutdown();
    return 0;
  }

  // Wait for device to ready
  ros::Duration(4).sleep();

  ROS_INFO("Beginning homing sequence");
  // Enable motors and perform auto-home
  motors_on = true;
  std_srvs::Trigger::Request rq;
  std_srvs::Trigger::Response rp;
  home(rq, rp);

  
  // Wait for homing to complete
  if (rp.success) {
    ROS_INFO("Homing successful.");
  } else {
    ROS_INFO("Homing failed.");
  }
  

  // Define subscribers, publishers, and services
  ros::Timer position_timer = nh.createTimer(ros::Duration(0.2), &publishPosition);
  ros::Timer motor_timer = nh.createTimer(ros::Duration(0.02), &simulateMotors);
  
  pub_setpoint = nh.advertise<sensor_msgs::JointState>("setpoint_js", 1000);
  pub_goal = nh.advertise<sensor_msgs::JointState>("goal_js", 1000);
  
  ros::Subscriber move_sub = nh.subscribe("move_jr", 1000, &move);
  ros::Subscriber move_to_sub = nh.subscribe("move_jp", 1000, &move_to);
  ros::Subscriber velocity_scale_sub = nh.subscribe("velocity_scale", 1000, &velocity_scale);
  
  ros::ServiceServer halt_serv = nh.advertiseService("halt", &halt);
  ros::ServiceServer tap_serv = nh.advertiseService("tap", &tap);
  ros::ServiceServer home_serv = nh.advertiseService("home", &home);
  ros::ServiceServer set_endeff_serv = nh.advertiseService("set_endeff", &set_endeff);
  ros::ServiceServer set_led_serv = nh.advertiseService("set_led", &set_led);
  ros::ServiceServer set_motors_serv = nh.advertiseService("set_motors", &set_motors);
 
  
  // Release flow control to ROS
  ros::spin();
  
  return 0;
}
