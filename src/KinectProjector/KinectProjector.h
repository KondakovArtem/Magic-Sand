/***********************************************************************
KinectProjector - KinectProjector takes care of the spatial conversion
between the various coordinate systems, control the kinectgrabber and
perform the calibration of the kinect and projector.
Copyright (c) 2016-2017 Thomas Wolf and Rasmus R. Paulsen (people.compute.dtu.dk/rapa)

This file is part of the Magic Sand.

The Magic Sand is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The Magic Sand is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with the Magic Sand; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
***********************************************************************/

#ifndef __GreatSand__KinectProjector__
#define __GreatSand__KinectProjector__

#include "ofxLibwebsockets.h"
#include <iostream>
#include "ofMain.h"
#include "ofxOpenCv.h"
#include "ofxCv.h"
#include "KinectGrabber.h"
#include "ofxModal.h"


#include "KinectProjectorCalibration.h"
#include "Utils.h"
#include "TemporalFrameFilter.h"
#include "Poco/Base64Encoder.h"

// component names
constexpr auto CMP_SPATIAL_FILTERING = "Spatial filtering";
constexpr auto CMP_DRAW_KINECT_DEPTH_VIEW = "Draw kinect depth view";
constexpr auto CMP_DRAW_KINECT_COLOR_VIEW = "Draw kinect color view";
constexpr auto CMP_DUMP_DEBUG = "Dump Debug";
constexpr auto CMP_QUICK_REACTION = "Quick reaction";
constexpr auto CMP_AVERAGING = "Averaging";
constexpr auto CMP_CEILING = "Ceiling";
constexpr auto CMP_TILT_X = "Tilt X";
constexpr auto CMP_TILT_Y = "Tilt Y";
constexpr auto CMP_VERTICAL_OFFSET = "Vertical offset";
constexpr auto CMP_SHOW_ROI_ON_SAND = "Show ROI on sand";
constexpr auto CMP_INPAINT_OUTLIERS = "Inpaint outliers";
constexpr auto CMP_FULL_FRAME_FILTERING = "Full Frame Filtering";

// application states
constexpr int APP_STATE_IDLE = 0;
constexpr int APP_STATE_SETUP = 1;  // Shows depth image and display white rectangle on projector
constexpr int APP_STATE_CALIBRATING = 2;
constexpr int APP_STATE_RUNNING = 3;

class ofxModalThemeProjKinect : public ofxModalTheme {
public:
    ofxModalThemeProjKinect()
    {
        animation.speed = 0.1f;
        fonts.title = ofxSmartFont::add("ofxbraitsch/fonts/HelveticaNeueLTStd-Md.otf", 20, "modal-title");
        fonts.message = ofxSmartFont::add("ofxbraitsch/fonts/Roboto-Regular.ttf", 16, "modal-message");
    }
};

class KinectProjector {
public:
    KinectProjector(std::shared_ptr<ofAppBaseWindow> const& p);
    
    bool forceGuiUpdate;

    // Running loop functions
    void setup(bool sdisplayGui);
    void update();
    void updateGuiValue();
    void updateNativeScale(float scaleMin, float scaleMax);
    void drawProjectorWindow();
    void drawMainWindow(float x, float y, float width, float height);
    void drawGradField();

    // Coordinate conversion functions
    ofVec2f worldCoordToProjCoord(ofVec3f vin);
	ofVec3f projCoordAndWorldZToWorldCoord(float projX, float projY, float worldZ);
	ofVec2f kinectCoordToProjCoord(float x, float y);
	ofVec2f kinectCoordToProjCoord(float x, float y, float z);
	
	ofVec3f kinectCoordToWorldCoord(float x, float y);
	ofVec2f worldCoordTokinectCoord(ofVec3f wc);
	ofVec3f RawKinectCoordToWorldCoord(float x, float y);
    float elevationAtKinectCoord(float x, float y);
    float elevationToKinectDepth(float elevation, float x, float y);
    ofVec2f gradientAtKinectCoord(float x, float y);

	// Try to start the application - assumes calibration has been done before
	string startApplication();
    string startApplication(bool updateFlag);

    // Setup & calibration functions
    void startFullCalibration();
    void startAutomaticROIDetection();
    string startAutomaticKinectProjectorCalibration(bool updateGui);
    void startAutomaticKinectProjectorCalibration();
    void setSpatialFiltering(bool sspatialFiltering, bool updateGui);
    void setGradFieldResolution(int gradFieldResolution);
	void updateStatusGUI();
    void setForceGuiUpdate(bool value);
    bool getSpatialFiltering();
    void setInPainting(bool inp, bool updateGui);
	bool getInPainting();
    void setFullFrameFiltering(bool ff, bool updateGui);
	bool getFullFrameFiltering();

	
	void setFollowBigChanges(bool sfollowBigChanges, bool updateGui);
    bool getFollowBigChanges();
	void StartManualROIDefinition();
	void ResetSeaLevel();
	void showROIonProjector(bool show);

    bool getShowROIonProjector();

    // Gui and event functions
    void setupGui();
    void onButtonEvent(ofxDatGuiButtonEvent e);

	void onToggleEvent(ofxDatGuiToggleEvent e);
    void setAveraging(float value);
    void setDrawKinectDepthView(bool value);
    bool getDrawKinectDepthView();
    void setDrawKinectColorView(bool value);
    bool getDrawKinectColorView();
    int getAveraging();
    void setCeiling(float value);
    float getCeiling();
    void setTilt(float tiltX, float tiltY);
    void setTiltX(float value);
    void setTiltY(float value);
    float getTiltX();
    float getTiltY();
    void setVerticalOffset(float value);
    float getVerticalOffset();
    void onSliderEvent(ofxDatGuiSliderEvent e);
    void onConfirmModalEvent(ofxModalEvent e);
	string onCancelCalibration(bool updateGui);
    string onConfirmCalibration();
    void onCalibModalEvent(ofxModalEvent e);

	void mousePressed(int x, int y, int button);
	void mouseReleased(int x, int y, int button);
	void mouseDragged(int x, int y, int button);

    bool externUpdate;

    // Functions for shaders
    void bind(){
        FilteredDepthImage.getTexture().bind();
    }
    void unbind(){
        FilteredDepthImage.getTexture().unbind();
    }
    ofMatrix4x4 getTransposedKinectWorldMatrix(){
        return kinectWorldMatrix.getTransposedOf(kinectWorldMatrix);
    } // For shaders: OpenGL is row-major order and OF is column-major order
    ofMatrix4x4 getTransposedKinectProjMatrix(){
        return kinectProjMatrix.getTransposedOf(kinectProjMatrix);
    }
	// Depending on the mount direction of the Kinect, projections can be flipped. 
	bool getProjectionFlipped();


    // Getter and setter
    ofTexture & getTexture(){
        return FilteredDepthImage.getTexture();
    }
    void setKinectROI(int x, int y, int width, int height) {
        kinectROI = ofRectangle(x, y, width, height);
        setNewKinectROI(false);
    }
    ofRectangle getKinectROI(){
        return kinectROI;
    }
    ofVec2f getKinectRes(){
        return kinectRes;
    }
    ofVec4f getBasePlaneEq(){
        return basePlaneEq;
    }
    ofVec3f getBasePlaneNormal(){
        return basePlaneNormal;
    }
    ofVec3f getBasePlaneOffset(){
        return basePlaneOffset;
    }

	// Get the ROI of the projector window that should match the Kinect ROI
	ofRectangle getProjectorActiveROI();

	// Map Game interface
	bool getBinaryLandImage(ofxCvGrayscaleImage& BinImg);

	bool isCalibrated(){
        return projKinectCalibrated;
    }
    bool isImageStabilized(){
        return imageStabilized;
    }
    bool isBasePlaneUpdated(){ // To be called after update()
        return basePlaneUpdated;
    }
    void setBasePlaneUpdated(bool val) { // To be called after update()
        basePlaneUpdated = val;
    }
    //bool isROIUpdated(){ // To be called after update()  // Could be set using manual mouse based drawing and cleared before the information was propagated to other modules
    //    return ROIUpdated;
    //}
    bool isCalibrationUpdated(){ // To be called after update()
        return projKinectCalibrationUpdated;
    }

	// The overall application stat
	enum Application_state
	{
		APPLICATION_STATE_IDLE = APP_STATE_IDLE,
		APPLICATION_STATE_SETUP = APP_STATE_SETUP,  // Shows depth image and display white rectangle on projector
		APPLICATION_STATE_CALIBRATING = APP_STATE_CALIBRATING,
		APPLICATION_STATE_RUNNING = APP_STATE_RUNNING
	};

    enum Calibration_state
    {
        CALIBRATION_STATE_FULL_AUTO_CALIBRATION = 0,
        CALIBRATION_STATE_ROI_AUTO_DETERMINATION = 1,
        CALIBRATION_STATE_ROI_MANUAL_DETERMINATION = 2,
        CALIBRATION_STATE_ROI_FROM_FILE = 3,
        CALIBRATION_STATE_PROJ_KINECT_AUTO_CALIBRATION = 4,
        CALIBRATION_STATE_PROJ_KINECT_MANUAL_CALIBRATION = 5
    };

    enum Auto_calibration_state
    {
        AUTOCALIB_STATE_INIT_FIRST_PLANE = 0,
        AUTOCALIB_STATE_INIT_POINT = 1,
        AUTOCALIB_STATE_NEXT_POINT = 2,
        AUTOCALIB_STATE_COMPUTE = 3,
        AUTOCALIB_STATE_DONE = 4
    };

	Application_state GetApplicationState()
	{
		return applicationState;
	}

    Calibration_state GetCalibrationState()
    {
        return calibrationState;
    }

    void setAutoCalibrationState(Auto_calibration_state newValue);
    auto GetAutoCalibrationState() {
        return autoCalibState;
    }

	bool getDumpDebugFiles();

    void setDumpDebugFiles(bool value);

	// Debug functions
	void SaveFilteredDepthImage();
	void SaveKinectColorImage();
    string getKinectColorImage();

    ofxDatGui* getGui();

    //void setBroadcastMethod(std::function<void(Json::Value)> method);
    //void setBroadcastStateMethod(std::function<void()> fn);

    std::function<void(Json::Value)> broadcast;
    std::function<void()> broadcastState;

private:

    
    enum Full_Calibration_state
    {
        FULL_CALIBRATION_STATE_ROI_DETERMINATION,
        FULL_CALIBRATION_STATE_AUTOCALIB,
        FULL_CALIBRATION_STATE_DONE
    };
    enum ROI_calibration_state
    {
        ROI_CALIBRATION_STATE_INIT,
        ROI_CALIBRATION_STATE_READY_TO_MOVE_UP,
        ROI_CALIBRATION_STATE_MOVE_UP,
        ROI_CALIBRATION_STATE_DONE
    };
    

   
    void exit(ofEventArgs& e);
    void setupGradientField();
    

    void updateCalibration();
    void updateFullAutoCalibration();
    void updateROIAutoCalibration();
    void updateROIFromColorImage();
    void updateROIFromDepthImage();
	void updateROIFromFile();
	
//	void updateROIManualCalibration();
    void updateROIFromCalibration();
    void setMaxKinectGrabberROI();
    void setNewKinectROI();
    void setNewKinectROI(bool updateGui);
    void updateKinectGrabberROI(ofRectangle ROI);

	void updateProjKinectAutoCalibration();

	double ComputeReprojectionError(bool WriteFile);
	void CalibrateNextPoint();

	void updateProjKinectManualCalibration();
    bool addPointPair();
    void updateMaxOffset();
    void updateBasePlane();
    void askToFlattenSand();
    bool askToFlattenSandFlag;

    void drawChessboard(int x, int y, int chessboardSize);
    void drawArrow(ofVec2f projectedPoint, ofVec2f v1);

    void saveCalibrationAndSettings();
    bool loadSettings();
    bool saveSettings();
    
	void ProcessChessBoardInput(ofxCvGrayscaleImage& image);
	void CheckAndNormalizeKinectROI();

    // State variables
    bool secondScreenFound;
	bool kinectOpened;
	float lastKinectOpenTry;
	bool ROIcalibrated;
    bool projKinectCalibrated;
//    bool ROIUpdated;
    bool projKinectCalibrationUpdated;
	bool basePlaneComputed;
    bool basePlaneUpdated;
    bool imageStabilized;
    bool waitingForFlattenSand;
    bool drawKinectView;
	bool drawKinectColorView;
    Calibration_state calibrationState;
    ROI_calibration_state ROICalibState;
    Auto_calibration_state autoCalibState;
    Full_Calibration_state fullCalibState;
	Application_state applicationState;

    // Projector window
    std::shared_ptr<ofAppBaseWindow> projWindow;
    
    //kinect grabber
    KinectGrabber               kinectgrabber;
    bool                        spatialFiltering;
    bool                        followBigChanges;
    int                         numAveragingSlots;
	bool                        doInpainting;
	bool                        doFullFrameFiltering;

    float tiltY;
    float tiltX;
    float verticalOffset;

    //kinect buffer
    ofxCvFloatImage             FilteredDepthImage;
    ofxCvColorImage             kinectColorImage;
    ofVec2f*                    gradField;
	ofFpsCounter                fpsKinect;
	ofxDatGuiTextInput*         fpsKinectText;

    // Projector and kinect variables
    ofVec2f projRes;
    ofVec2f kinectRes;

    // FBos
    ofFbo fboProjWindow;
    ofFbo fboMainWindow;

    //Images and cv matrixes
    cv::Mat                     cvRgbImage;
	cv::Mat                     cvGrayImage;
//	ofxCvFloatImage             Dptimg;
    
    //Gradient field variables
    int gradFieldcols, gradFieldrows;
    int gradFieldResolution;
    float arrowLength;
    int fishInd;
    
    // Calibration variables
    ofxKinectProjectorToolkit*  kpt;
    vector<ofVec2f>             currentProjectorPoints;
    vector<cv::Point2f>         cvPoints;
    vector<ofVec3f>             pairsKinect;
    vector<ofVec2f>             pairsProjector;

    // ROI calibration variables
    ofxCvGrayscaleImage         thresholdedImage;
    ofxCvContourFinder          contourFinder;
    float                       threshold;
    ofPolyline                  large;
    ofRectangle                 kinectROI, kinectROIManualCalib;
	ofVec2f                     ROIStartPoint;
	ofVec2f                     ROICurrentPoint;
	bool                        doShowROIonProjector;

    // Base plane
    ofVec3f basePlaneNormal, basePlaneNormalBack;
    ofVec3f basePlaneOffset, basePlaneOffsetBack;
    ofVec4f basePlaneEq; // Base plane equation in GLSL-compatible format
    
    // Conversion matrices
    ofMatrix4x4                 kinectProjMatrix;
    ofMatrix4x4                 kinectWorldMatrix;

    // Max offset for keeping kinect points
    float maxOffset;
    float maxOffsetSafeRange;
    float maxOffsetBack;
    
    // Autocalib points
    ofPoint* autoCalibPts; // Center of autocalib chess boards
    int currentCalibPts;
    int trials;
    bool upframe;

	// Temporal frame filter for cleaning the colour image used for calibration. It should probably be moved to the grabber class/thread
	CTemporalFrameFilter TemporalFrameFilter;
	// Keeps track of how many frames are acquired since last calibration event
	int TemporalFrameCounter;
	// Type of temporal filtering of colour image 0: Median, 1 :average
	int TemporalFilteringType;

    // Chessboard variables
    int   chessboardSize;
    int   chessboardX;
    int   chessboardY;

    // GUI Modal window & interface
	bool displayGui;
    shared_ptr<ofxModalConfirm>   confirmModal;
    shared_ptr<ofxModalAlert>   calibModal;
    shared_ptr<ofxModalThemeProjKinect>   modalTheme;
    ofxDatGui* gui;
	ofxDatGui* StatusGUI;
	std::string calibrationText;
	
	// Debug functions
	bool DumpDebugFiles;
	std::string DebugFileOutDir;
	std::string GetTimeAndDateString();
	bool savePointPair();
	void SaveFilteredDepthImageDebug();
};


#endif /* defined(__GreatSand__KinectProjector__) */


