// FindRob.h: interface for the FindRob class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_FINDROB_H__72A23126_B93C_4525_9F91_B5C7F379DC83__INCLUDED_)
#define AFX_FINDROB_H__72A23126_B93C_4525_9F91_B5C7F379DC83__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <iostream>
#include <string>
#include <math.h>
#include <highgui.h>
#include <cv.h>

using namespace std;

class FindRob  
{
public:
  //Attention:you can't do any changes to the img outside until you don't use this class
	FindRob(IplImage *img);//input picture, NULL is ok
	virtual ~FindRob();
  void ReInit(IplImage *img);//change picture, NULL is not allowed!

  //about robot
  CvPoint getRobCenter();//get the center of robot
  int getRobRadius();//get the radius of robot
  double getRobDir();//get robot direction(asin), forward is 0, left is -pai~0, right is 0~+pai
  bool doesRobotExist();

  //about ground,about yellow
  double getGroundDir();//return direction of ground according to yellow grid, + means plane should turn left
  int isGroundEdge();//does edge appear in the camera?0-no, 8-forward, 4-back, 2-left, 1-right, 10-forward left, 9-forward right, 6-back left, 5-back right
  
  //GroundCenter,about blue
  bool doesGroundCenterExist();
  CvPoint getGroundCenter();//return blue point center,if not exist, return (-100,-100)
  int getGroundCenterRadius();//return blue point Radius

private:
  IplImage *OriginImg,
    *OriImg,
    *ImgForRob,
    *ImgForYellow,
    *ImgForBlue;
  CvPoint RobotCenter;
  int RobotRadius;
  CvPoint RobotBlackPoint;
  CvPoint GroundCenter;
  int GroundRadius;
  int isEdge,//from yellow
    isEdgeConfirm;//combine yellow and blue
  bool FlagRobotExist;
  bool FlagGroundCenterExist; 
  int minX, maxX, minY, maxY;//to remember the area which is not outside edge
  double GroundDir;

  void SearchRobot(IplImage *src);
  bool FlagSearRob;
  void AnalyzeGround(IplImage *src);
  bool FlagAnaGrou;
  void FindGroundCenter(IplImage *src);
  bool FlagFindGrCenter;

  double angle( CvPoint* pt1, CvPoint* pt2, CvPoint* pt0 );//finds a cosine of angle between vectors,from pt0->pt1 and from pt0->pt2
  double ptdistance(CvPoint* pt1, CvPoint* pt2);//cal distance of two points
};

#endif // !defined(AFX_FINDROB_H__72A23126_B93C_4525_9F91_B5C7F379DC83__INCLUDED_)
