
/*
 * main.cpp
 *
 *  Created on: May 14, 2015
 *      Author: ljw
 */

#include <iostream>
#include <iomanip>
#include "ros/ros.h"
#include "math/SL_Matrix.h"
#include "fstream"
#include "std_msgs/String.h"

#include "imgproc/SL_Image.h"
#include "imgproc/SL_ImageIO.h"

#include "tools/GUI_ImageViewer.h"
#include "tools/SL_Print.h"
#include "tools/SL_DrawCorners.h"

#include "AffineTransform.h"
#include "ARDrone.h"
#include "ArdroneTf.h"
#include "CMDReciever.h"
#include "GridDetector.h"
#include "IMURecorder.h"
#include "FindRob.h"
#include "PIDController.h"
#include "PredictNumber.h"
#include "ROSThread.h"
#include "VideoRecorder.h"
#include "SearchNumber.h"
#include "PID.h"
#include "ExternalCamera.h"
#include "NavIntegration.h"

#include "time.h"
#include "stdlib.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include "opencv2/legacy/blobtrack.hpp"
#include "math.h"

#include <ros/ros.h>
#include <keyboard/Key.h>
#include <tf/transform_listener.h>
#include <tf/transform_broadcaster.h>

using namespace std;

static int mGrids = 5;
static int nGrids = 6;
static int setcamera = 0;

void writeFeatPts(const vector<cv::Point2f> &featPts, const char *filePath) {
  ofstream file(filePath);
  if (!file)
    repErr("cannot open '%s' to write!", filePath);

  for (size_t i = 0; i < featPts.size(); i++) {
    file << featPts[i].x << " " << featPts[i].y << endl;
  }
}

void writeIMUMeas(const vector<IMUData> &imumeas, const char *filePath) {
  std::ofstream file(filePath);
  if (!file)
    repErr("cannot open '%' to write!", filePath);

  for (size_t i = 0; i < imumeas.size(); i++) {
    imumeas[i].write(file);
  }
  file.close();
}

void LogCurTime(ofstream &log) {
  time_t timep;
  struct tm *a;
  time(&timep);
  a = localtime(&timep);
  log << endl
      << a->tm_mday << " " << a->tm_hour << ":" << a->tm_min << ":" << a->tm_sec
      << endl;
}

#define CLIP3(_n1, _n, _n2)                                                    \
  {                                                                            \
    if (_n < _n1)                                                              \
      _n = _n1;                                                                \
    if (_n > _n2)                                                              \
      _n = _n2;                                                                \
  }
ofstream fout("/home/mozhi/Record/test.txt", ios::app);

void *Control_loop(void *param) {
  ARDrone drone;
  drone.setup();
  ArdroneTf drone_tf("/home/mozhi/Logs/tf.txt");
  NavIntegration drone_NI;
  CMDReciever cmdreader("/home/mozhi/Logs/cmd.txt", drone);
  IMURecorder imureader("/home/mozhi/Record/imu.txt");
  VideoRecorder videoreader("/home/mozhi/Record/video_ts.txt",
                            "/home/mozhi/Record/video.avi");

  ExternalCamera ex_cam(0.2);
  double img_time;
  ROSThread thread(imureader, videoreader, cmdreader, ex_cam, drone_NI);
  thread.showVideo = true;
  ros::Rate loop_rate(50);
  ////////////////////////////////
  FindRob find_rob(NULL);
  IplImage *imgsrc;
  CvSize imgSize = {640, 360};
  imgsrc = cvCreateImage(imgSize, IPL_DEPTH_8U, 3);
  Mat imgmat;
  cv::Mat ex_img;
  
  system("rosservice call /ardrone/setcamchannel 1");
  //system("rosservice call /ardrone/flattrim");
  ///////////////////////// PID control parameters
  double centerx, centery;
  double errorx, errory, errorturn;
  double lasterrorx, lasterrory, lasterrorturn;
  double targetvx, targetvy, targetv;
  int targeth, targethland = 700;
  double leftr = 0, forwardb = 0, upd = 0, turnleftr = 0;
  double vk = 2.0; // 0.001;
  static double kp = 4.0; // 0.0001;
  static double kd = 150.0;
  static double ki = 0.0;
  double scale = 3;
  static double vkp = 5, vkd = 20, vki = 0;
  ///////////////////////////////////PID���ڵĳ�ʼ��///////////////////////////////
  PID pid(thread, find_rob);
  //////////////////////////////////////////////////////////
  ofstream log;
  char filename[50];
  time_t timep;
  struct tm *a;
  time(&timep);
  a = localtime(&timep);
  sprintf(filename, "/home/mozhi/Logs/%02d_%02d_%02d_%02d.txt", a->tm_mday,
          a->tm_hour, a->tm_min, a->tm_sec);

  log.open(filename);
  if (!log) {
    cout << "cannot open file to log" << endl;
  }
  cout << "Start!" << endl;
  ///////////////////////////////////////////////////////////
  ModeType cur_mode = STOP, next_mode = STOP;
  int frame_count = 0, lostframe = 0;
  ///////////////////////////////////////////////////////////
  double robot_x = 0, robot_y = 0;
  double last_robot_x, last_robot_y;
  double drone_x, drone_y;
  double targetx = 320, targety = 120;
  double flying_scale = 300;
  double takeoff_altitude = 1800;

  int pid_stable_count = 0;
  int serve_stable_count = 0;
  const int Edge_Forward = 8, Edge_Back = 4, Edge_Left = 2, Edge_Right = 1;

  bool robot_exist = true;
  bool serving_flag = false;
  double search_target_x, search_target_y;
  double last_robot_dir = 0;
  // Range: 0 ~ 1.8
  double random_angle = 0;

  enum SearchState { Search_Try_Back, Search_Try_Left, Search_Forward, 
      Search_Back, Search_Left, Search_Right, Search_Start};

  SearchState search = Search_Start;
  int edge;

  cvNamedWindow("Drone_Video", 1);
  cv::namedWindow("Ex_Video");
  cvMoveWindow("Drone_Video", 600, 350);
  srand(time(NULL));
  /*
  while(ros::ok()) {
    ex_cam.getRobotPosition(robot_x, robot_y);
    //std::cout << robot_x << std::endl;
    //std::cout << robot_y << std::endl;
    //ex_cam.isRobotForward();
  }
  */
  
  while (ros::ok()) {
    usleep(1000);
    if (ex_cam.getCurImage(ex_img)) {
      cv::imshow("Ex_Video", ex_img);
    }
    cv::waitKey(1);
    if (videoreader.newframe) {
      cur_mode = cmdreader.GetMode();
      frame_count++;
      lostframe = 0;
      videoreader.newframe = false;
      cout << "Battery:" << thread.navdata.batteryPercent << endl;
      videoreader.getImage(imgmat, img_time);
      *imgsrc = imgmat;
      find_rob.ReInit(imgsrc);
      find_rob.doesRobotExist();
      switch (cur_mode) {
      case START:
        LogCurTime(log);
        log << "START!!! TAKEOFF NOW" << std::endl;
        drone.hover();
        if (find_rob.doesRobotExist()) {
          next_mode = FOLLOWROBOT;
          random_angle = double(random() % 18) / 10;
          serving_flag = true;
        }
        else if (find_rob.doesGroundCenterExist()) {
          next_mode = WAITING;
          robot_exist = true;
          serving_flag = false;
        }
        //next_mode = OdoTest;
        break;
      case WAITING:
        LogCurTime(log);
        log << "WAITING!!" << std::endl;
        if (find_rob.doesGroundCenterExist()) {
          centerx = find_rob.getGroundCenter().x;
          centery = find_rob.getGroundCenter().y;
          CLIP3(10.0, centerx, 590.0);
          CLIP3(10.0, centery, 350.0);
          errory = centery - targety;
          errorx = centerx - targetx;
          forwardb = pid.PIDXY(errory, 1500);
          leftr = pid.PIDXY(errorx, 1500, false);
          upd = pid.PIDZ(55, 10, false);
          CLIP3(-0.1, leftr, 0.1);
          CLIP3(-0.1, forwardb, 0.1);
          CLIP3(-0.2, upd, 0.2);
          turnleftr = 0;

          if (abs(errorx) < 30 && abs(errory) < 30 && upd == 0) {
            errorturn = find_rob.getGroundDir();
            turnleftr = errorturn * 10;
            CLIP3(-0.15, turnleftr, 0.15);
            if(abs(errorturn) < 0.08) {
              turnleftr = 0;
              log << "TakeOff Complete!!! Waitting Ex_Camera" << std::endl;
              if (ex_cam.isRobotExists() && !ex_cam.isRobotForward()) {
                log << "RobotExists" << std::endl;
                drone_NI.Clear();
                next_mode = TOROBOT;
                pid.PIDReset();
              }
              if(find_rob.doesRobotExist()) {
                next_mode = FOLLOWROBOT;
                random_angle = double(random() % 18) / 10;
                pid.PIDReset();
              }
              if(!robot_exist) {
                if(ex_cam.isRobotExists()) {
                  robot_exist = true;
                  log << "Robot First Found" << std::endl;
                  drone_NI.Clear();
                  next_mode = TOROBOT;
                  pid.PIDReset();
                }
              }
            }
            else {
              log << "turning" << std::endl;
            }
          }
          
          if(robot_exist) {
            if(!ex_cam.isRobotExists()) {
              pid_stable_count++;
              if(pid_stable_count > 5) {
                log << "Robot Disappare!" << std::endl;
                pid_stable_count = 0;
                robot_exist = false;
              }
            }
            else {
              pid_stable_count = 0;
            }
          }
          log << "LastExists = " << robot_exist << std::endl;
          log << "RobotExists = " << ex_cam.isRobotExists() << std::endl;
          log << "altd = " << thread.navdata.altd << std::endl;
          log << "upd = " << upd << std::endl;
          log << "radius = " << find_rob.getGroundCenterRadius() << std::endl;
        }
        break;
      case TOCENTER:
        LogCurTime(log);
        log << "TOCENTER!!!" << std::endl;
        drone_NI.Get(drone_x, drone_y);
        errorx = drone_x + last_robot_x;
        errory = drone_y + last_robot_y;
        forwardb = pid.PIDXY(errorx * flying_scale, 500);
        leftr = pid.PIDXY(errory * flying_scale, 500, false);
        CLIP3(-0.1, leftr, 0.1);
        CLIP3(-0.1, forwardb, 0.1);
        upd = 0;
        turnleftr = 0;
        if(abs(errorx) < 0.8 && abs(errory) < 0.8) {
          if (find_rob.doesGroundCenterExist()) {
            pid_stable_count++;
            if(pid_stable_count >=4) {
              next_mode = WAITING;
              robot_exist = true;
              pid_stable_count = 0;
              pid.PIDReset();
            }
          }
          else {
            pid_stable_count = 0;
        }
        }
        log << "errorx = " << errorx << " errory = " << errory << std::endl;
        log << "last_robot_x = " << last_robot_x << " last_robot_y" 
            << last_robot_y << std::endl;

        break;
      case TOROBOT:
        LogCurTime(log);
        log << "TOROBOT" << std::endl;
        drone_NI.Get(drone_x, drone_y);
        ex_cam.getRobotPosition(robot_x, robot_y);
        errorx = drone_x - robot_x;
        errory = drone_y - robot_y;
        forwardb = pid.PIDXY(errorx * flying_scale, 500);
        leftr = pid.PIDXY(errory * flying_scale, 500, false);
        CLIP3(-0.15, leftr, 0.15);
        CLIP3(-0.15, forwardb, 0.15);
        upd = 0;
        turnleftr = 0;
        if (find_rob.doesRobotExist()) {
          pid_stable_count++;
          if (pid_stable_count >= 3) {
            log << "FIND ROBOT!! Follow it!" << std::endl;
            next_mode = FOLLOWROBOT;
            random_angle = double(random() % 18) / 10;
            pid_stable_count = 0;
            pid.PIDReset();
          }
        }
        else {
          pid_stable_count = 0;
        }
        
        log << "errorx = " << errorx << "  forward = " << forwardb << std::endl;
        log << "errory = " << errory << "  leftr = " << leftr << std::endl;
        log << "robotx = " << robot_x << " roboty = " << robot_y << std::endl;
        
        break;
      case FOLLOWROBOT:
        LogCurTime(log);
        log << "Fllow Robot" << std::endl;
        if (find_rob.doesRobotExist()) {
          centerx = find_rob.getRobCenter().x;
          centery = find_rob.getRobCenter().y;
          CLIP3(10.0, centerx, 590.0);
          CLIP3(10.0, centery, 350.0);
          errory = centery - targety;
          errorx = centerx - targetx;
          forwardb = pid.PIDXY(errory, 800);
          leftr = pid.PIDXY(errorx, 800, false);
          upd = pid.PIDZ(70, 10);
          CLIP3(-0.1, leftr, 0.1);
          CLIP3(-0.1, forwardb, 0.1);
          CLIP3(-0.2, upd, 0.2);
          turnleftr = 0;

          log << "rob direction = " << find_rob.getRobDir() << std::endl;
          log << "Leave dir: 0.5 ~ " << 2.5 - random_angle << std::endl;
          if (serving_flag) {
            if (last_robot_dir > find_rob.getRobDir()) {
              serve_stable_count++;
              if (serve_stable_count > 20) {
                log << "Yes!!!!" << std::endl;
                serving_flag = false;
              }
            }
            else {
              serve_stable_count = 0;
            }
            last_robot_dir = find_rob.getRobDir();
          }

          if (abs(errorx) < 30 && abs(errory) < 30) {
            forwardb = 0;
            leftr = 0;
            if (find_rob.getRobDir() > 0.5 && 
                find_rob.getRobDir() < (2.5 - random_angle) && !serving_flag) {

              ex_cam.getRobotPosition(robot_x, robot_y);
              last_robot_x = robot_x;
              last_robot_y = robot_y;
              drone_NI.Clear();
              pid.PIDReset();
              if (abs(robot_x) > 0.8 && abs(robot_y) > 0.8) {
                next_mode = TOCENTER;
              }
              else {
                next_mode = LEAVEROBOT;
              }
              log << "Yes !!! We should Leave Robot" << std::endl;
            }
          }
        } else {
          upd = 0;
          log << "Cannot Find Robot." << std::endl;
        }
        break;
      case LEAVEROBOT:
        LogCurTime(log);
        log << "Leaving Robot!!" << std::endl;
        drone_NI.Get(drone_x, drone_y);
        errorx = drone_x + 1;
        errory = drone_y;
        forwardb = pid.PIDXY(errorx * flying_scale, 500);
        leftr = pid.PIDXY(errory * flying_scale, 500, false);
        CLIP3(-0.1, leftr, 0.1);
        CLIP3(-0.1, forwardb, 0.1);
        upd = 0;
        turnleftr = 0;
        
        if (!find_rob.doesRobotExist()) {
          next_mode = TOCENTER;
          pid.PIDReset();
        }
        
        break;
      case SEARCHING:
        LogCurTime(log);
        log << "SEARCHING:  ";
        edge = find_rob.isGroundEdge();
        switch (search) {
        case Search_Start:
          log << "Start" << std::endl;
          drone_NI.Clear();
          pid.PIDReset();
          if ((edge&Edge_Back) != 0) {
            search = Search_Forward;
          }
          else if ((edge&Edge_Forward) != 0) {
            search = Search_Back;
          }
          else {
            search = Search_Try_Back;
          }
          break;
        case Search_Try_Back:
          log << "Try_Back" << std::endl;
          search_target_x = -3;
          search_target_y = 0;
          drone_NI.Get(drone_x, drone_y);
          if ((edge&Edge_Back) != 0) {
            search = Search_Forward;
            drone_NI.Clear();
            pid.PIDReset();
          }
          break;
        case Search_Back:
          log << "back" << std::endl;
          search_target_x = -1.5;
          search_target_y = 0;
          drone_NI.Get(drone_x, drone_y);
          if (abs(drone_x - search_target_x) < 0.05) {
            if ((edge&Edge_Left) != 0) {
              search = Search_Right;
            }
            else if ((edge&Edge_Right) != 0) {
              search = Search_Left;
            }
            else {
              search = Search_Try_Left;
            }
            drone_NI.Clear();
            pid.PIDReset();
          }
          break;
        case Search_Forward:
          log << "forward" << std::endl;
          search_target_x = 1.5;
          search_target_y = 0;
          drone_NI.Get(drone_x, drone_y);
          if (abs(drone_x - search_target_x) < 0.05) {
            if ((edge&Edge_Left) != 0) {
              search = Search_Right;
            }
            else if ((edge&Edge_Right) != 0) {
              search = Search_Left;
            }
            else {
              search = Search_Try_Left;
            }
            drone_NI.Clear();
            pid.PIDReset();
          }
          break;
        case Search_Try_Left:
          log << "Try Left" << std::endl;
          search_target_x = 0;
          search_target_y = 3;
          drone_NI.Get(drone_x, drone_y);
          if ((edge&Edge_Left) != 0) {
            search = Search_Right;
            drone_NI.Clear();
            pid.PIDReset();
          }
          break;
        case Search_Left:
          log << "Left" << std::endl;
          search_target_x = 0;
          search_target_y = 1.5;
          drone_NI.Get(drone_x, drone_y);
          if (abs(drone_y - search_target_y) < 0.05) {
            search = Search_Try_Back;
            drone_NI.Clear();
            pid.PIDReset();
          }
          break;
        case Search_Right:
          log << "Right" << std::endl;
          search_target_x = 0;
          search_target_y = -1.5;
          drone_NI.Get(drone_x, drone_y);
          if (abs(drone_y - search_target_y) < 0.05) {
            search = Search_Try_Back;
            drone_NI.Clear();
            pid.PIDReset();
          }
          break;
        }
        drone_NI.Get(drone_x, drone_y);
        errorx = drone_x - search_target_x;
        errory = drone_y - search_target_y;
        forwardb = pid.PIDXY(errorx * flying_scale, 500);
        leftr = pid.PIDXY(errory * flying_scale, 500, false);
        CLIP3(-0.1, leftr, 0.1);
        CLIP3(-0.1, forwardb, 0.1);
        upd = 0;
        turnleftr = 0;
        if (find_rob.doesGroundCenterExist()) {
          pid_stable_count++;
          if (pid_stable_count > 4) {
            next_mode = WAITING;
            robot_exist = true;
            pid_stable_count = 0;
          }
        }
        else {
          pid_stable_count = 0;
        }
        log << "errorx = " << errorx << " errory = " << errory << std::endl;
        log << "targetx = " << search_target_x 
            << " targety = " << search_target_y << std::endl;

        break;
      case OdoTest:
        drone_tf.SetRefPose(0, img_time);
        drone_NI.Clear();
        robot_x = 1;
        robot_y = 1;
        next_mode = LEAVEROBOT;
        break;
      default:
        break;
      }
      std::cout << find_rob.isGroundEdge() << std::endl;
    }
    lostframe++;
    if (lostframe > 3000) {
      drone.land(); // if the video is not fluent
      continue;
    }
    if (lostframe > 100) {
      cout << "stuck." << endl;
      drone.hover(); // if the video is not fluent
      continue;
    }
    if (cur_mode == cmdreader.GetMode() && cur_mode != MANUL) {
      cmdreader.RunNextMode(next_mode, leftr, forwardb, upd, turnleftr);
    }
    if (cmdreader.GetMode() == MANUL) {
      if (static_cast<double>(clock() - cmdreader.GetManualTime()) /
        CLOCKS_PER_SEC * 1000 >
        1000) {

        drone.hover();
      }
    }
    cvShowImage("Drone_Video", imgsrc);
  }
  drone.land();
  cvReleaseImage(&imgsrc);
  return 0;
}

void ROSControl_main(int argc, char **argv) {
  // ros::init(argc, argv, "listener");
  pthread_t ROS_thread, control_thread;
  int rc = pthread_create(&control_thread, NULL, Control_loop, 0);
  if (rc) {
    printf("ERROR; return code from pthread_create() is %d\n", rc);
    exit(-1);
  }

  pthread_join(control_thread, NULL);
}

CvANN_MLP bp;
#include "IMUVideoSync.h"
int main(int argc, char **argv) {
#if premode == 1
  bp.load("/home/mozhi/catkin_ws/src/Ardrone_L-H/src/NumberTrain/bpModel1.xml");
#else
#if premode == 2
  bp.load("/home/mozhi/catkin_ws/src/Ardrone_L-H/src/NumberTrain/bpModel2.xml");
#else
  bp.load(
    "/home/mozhi/catkin_ws/src/Ardrone_L-H/src/NumberTrain/bpModel_op.xml");
#endif
#endif
  ros::init(argc, argv, "ARDrone_test");
  ROSControl_main(argc, argv);
  fout.close();

  return 0;
}
