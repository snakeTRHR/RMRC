#include <cmath>
#include <ros/ros.h>
#include <ros/time.h>
#include <sensor_msgs/JointState.h>
#include <sensor_msgs/Joy.h>
#include <std_msgs/Float64.h>
#include <std_msgs/Float64MultiArray.h>
#include <trajectory_msgs/JointTrajectory.h>
#include <vector>
#include "robot_motion/semi_autonomous.h"
#include "dynamixel/dynamixel.h"
#include <dynamixel_workbench_msgs/DynamixelCommand.h>

using std::vector;

#define DEBUG 1
#define front_right 0
#define front_left 1
#define rear_right 2
#define rear_left 3

constexpr double original_theta = 90.0;
constexpr double autonomous_max_theta = 75.0;
constexpr double autonomous_min_theta = -90.0;
constexpr double Kp = 1.0;
constexpr double Kd = 1.0;
constexpr double frequency = 45;
vector<double> current_dynamixel_theta{0, 0, 0, 0};
vector<double> current_dynamixel_torque{0, 0, 0, 0};
vector<double> angle_goal{0, 0, 0, 0};
vector<double> angle_now{0, 0, 0, 0};
double theta_ref[4]{0, 0, 0, 0};
double gyro_robot{};
double theta_front{};
double theta_rear{};
double torque_front{};
double torque_rear{};

//------------------------------------------------------------------
namespace all {
  bool check() {
    for (int i = 0; i < 4; ++i) {
      if ((current_dynamixel_theta[i] + 1 > theta_ref[i]) and (current_dynamixel_theta[i] - 1 < theta_ref[i])) {
      } else {
        return false;
      }
    }
    return true;
  }

  inline double reset() {
    for (int i = 0; i < 4; ++i) {
      theta_ref[i] = original_theta;
    }
  }
  inline double setForward() {
    if (all::check()) {
      theta_ref[0] += 1.3;
    }
    for (int i = 1; i < 4; ++i) {
      theta_ref[i] = theta_ref[0];
    }
  }
  inline double setReverse() {
    if (all::check()) {
      theta_ref[0] -= 1.3;
    }
    for (int i = 1; i < 4; ++i) {
      theta_ref[i] = theta_ref[0];
    }
  }
}
//--------------------------------------------------------------------
class flipper {
  private:
    int id;

  public:
    flipper(int user_id);
    void forward();
    void reverse();
};

flipper::flipper(int user_id) { id = user_id; }

void flipper::forward() {
  ROS_INFO("FORWARD %d", id);
  if ((current_dynamixel_theta[id] + 1 > theta_ref[id]) and (current_dynamixel_theta[id] - 1 < theta_ref[id])) {
#ifdef DEBUG
    ROS_INFO("OK_FORWARD %d", id);
    ROS_INFO("theta_ref[%d] %lf current_dynamixel_theta[%d] %lf", id, theta_ref[id], id, current_dynamixel_theta[id]);
#endif
    theta_ref[id] += 30;
#ifdef DEBUG
    ROS_INFO("theta_ref_result[%d] %lf", id, theta_ref[id]);
#endif
  }else if ((current_dynamixel_theta[id] + 1 > theta_ref[id]) and (current_dynamixel_theta[id] > 355.5)) {
    theta_ref[id] += 30;
  }
}

void flipper::reverse() {
  if ((current_dynamixel_theta[id] + 1 > theta_ref[id]) and (current_dynamixel_theta[id] - 1 < theta_ref[id])) {
#ifdef DEBUG
    ROS_INFO("OK_REVERSE %d", id);
    ROS_INFO("theta_ref[%d] %lf current_dynamixel_theta[%d] %lf", id, theta_ref[id], id, current_dynamixel_theta[id]);
#endif
    theta_ref[id] -= 30;
  }
}

//現在角度とトルクを取得
void jointStateCallback(const sensor_msgs::JointState &jointstate) {
  current_dynamixel_theta[0] = (jointstate.position[1] + M_PI) * 180 / M_PI;
  current_dynamixel_torque[0] = jointstate.effort[1];

  current_dynamixel_theta[1] = (jointstate.position[0] + M_PI) * 180 / M_PI;
  current_dynamixel_torque[1] = jointstate.effort[0];

  current_dynamixel_theta[2] = (jointstate.position[3] + M_PI) * 180 / M_PI;
  current_dynamixel_torque[2] = jointstate.effort[3];

  current_dynamixel_theta[3] = (jointstate.position[2] + M_PI) * 180 / M_PI;
  current_dynamixel_torque[3] = jointstate.effort[2];
}

//ロボットの現在角度を取得
void gyroCallback(const std_msgs::Float64 &msg) { gyro_robot = msg.data; }

bool buttons_reverse = false;
bool flag_semi_autonomous = false;
bool prev_semi_autonomous = false;
bool flag_all = false;
bool prev_all = false;
bool flag_reset = false;
bool prev_reset = false;
double axes_front_right = 0;
double axes_front_left = 0;
double buttons_rear_right = 0;
double buttons_rear_left = 0;

//コントローラ値を入力
void joyCallback(const sensor_msgs::Joy &controller) {
  buttons_reverse = controller.buttons[2];
  axes_front_right = controller.axes[5];
  axes_front_left = controller.axes[2];
  buttons_rear_right = controller.buttons[5];
  buttons_rear_left = controller.buttons[4];
  if ((prev_all == false) and controller.buttons[3] == true) {
    flag_all = !flag_all;
  }
  prev_all = controller.buttons[3];
  // Xboxキーが押されたらflag_semi_autonomousを切り替える
  if ((prev_semi_autonomous == false) and controller.buttons[8] == true) {
    flag_semi_autonomous = !flag_semi_autonomous;
  }
  prev_semi_autonomous = controller.buttons[8];
  // Bキーで全てのフリッパーの角度を90°
  if ((prev_reset == false) and controller.buttons[1] == true) {
    flag_reset = !flag_reset;
  }
  prev_reset = controller.buttons[1];
#ifdef DEBUG
  //ROS_INFO("flag_all %d flag_semi_autonomous %d", flag_all, flag_semi_autonomous);
#endif
}

//角度PID
inline void pidCal() {
  for (int i = 0; i < 4; ++i) {
    theta_ref[i] = Kp * (theta_ref[i] - theta_rear) +
      Kd * ((theta_ref[i] - theta_rear) / frequency);
  }
}

int main(int argc, char **argv) {
  ros::init(argc, argv, "semi_autonomous");
  ros::NodeHandle n;

  ros::ServiceClient dynamixel_service = n.serviceClient<dynamixel_workbench_msgs::DynamixelCommand>("/dynamixel_workbench/dynamixel_command");
  ros::Subscriber feedback_sub = n.subscribe("/dynamixel_workbench/joint_states", 10, jointStateCallback);
  ros::Subscriber gyro_sub = n.subscribe("gyro", 10, gyroCallback);
  ros::Subscriber controller_sub = n.subscribe("joy", 10, joyCallback);
  ros::Rate loop_rate(400);
  flipper position[4] = {0, 1, 2, 3};
  dynamixel servo[4] = {front_right, front_left, rear_right, rear_left};
  semiAutonomous robot_model(n);

  dynamixel_workbench_msgs::DynamixelCommand srv;
  int k = 0;
  while (ros::ok()) {
    while(angle_now[0] == 0.00){
      ROS_INFO("NO\n");
      ros::spinOnce();
    }
    for(int i = 0; i < 4; ++i){
      if(current_dynamixel_theta[i] <= 0)current_dynamixel_theta[i] = 360 + current_dynamixel_theta[i];
      ROS_INFO("theta_ref[%d] %lf current_dynamixel_theta[%d] %lf", i, theta_ref[i], i, current_dynamixel_theta[i]);
    }
    //半自動モードかどうか
    if (flag_semi_autonomous) {
      robot_model.main(theta_ref);
      //全てのフリッパーを同じように動かすか
    } else if (flag_all) {
      if ((axes_front_right < 0) or (axes_front_left < 0)) {
        all::setForward();
      }
      if ((buttons_rear_right == true) or (buttons_rear_left == true)) {
        all::setReverse();
      }
      if (flag_reset) {
        all::reset();
      }
      //個別でフリッパーを動かすか
    } else {
      if (buttons_reverse) {
        if (axes_front_right < 0) {
          position[0].reverse();
        }
        if (axes_front_left < 0) {
          position[1].reverse();
        }
        if (buttons_rear_right) {
          position[2].reverse();
        }
        if (buttons_rear_left) {
          position[3].reverse();
        }
      } else {
        if (axes_front_right < 0) {
          position[0].forward();
        }
        if (axes_front_left < 0) {
          position[1].forward();
        }
        if (buttons_rear_right) {
          position[2].forward();
        }
        if (buttons_rear_left) {
          position[3].forward();
        }
      }
    }
    // pidCal();
    for (int i = 0; i < 4; ++i) {
      if(theta_ref[i] > 360)theta_ref[i] = theta_ref[i] - 360;
      srv.request.command = "_";
      srv.request.id = i + 1;
      srv.request.addr_name = "Goal_Position";
      srv.request.value = servo[i].dynamixelSet(theta_ref[i], current_dynamixel_theta[i]);
      dynamixel_service.call(srv);
    }
    theta_front = theta_ref[0];
    theta_rear = theta_ref[2];
    ros::spinOnce();
    loop_rate.sleep();
  }
}
