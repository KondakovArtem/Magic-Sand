#pragma once
#include "ofxLibwebsockets.h"
#include "KinectProjector/KinectProjector.h"
#include "Games/BoidGameController.h"
#include "SandSurfaceRenderer.h";

// fields
constexpr auto FL_COMMAND = "command";
constexpr auto FL_APPLICATION_STATE = "applicationState";
constexpr auto FL_CALIBRATION_STATE = "calibrationState";
constexpr auto FL_AUTO_CALIBRATION_STATE = "autoCalibrationState";
constexpr auto FL_FULL_CALIB_STATE = "fullCalibState";
constexpr auto FL_ROI_CALIB_STATE = "ROICalibState";
constexpr auto FL_CONFIRM_MODAL_STATE = "confirmModalState";
constexpr auto FL_CONFIRM_MESSAGE = "confirmMessage";
constexpr auto FL_KINECT_OPENED = "kinectOpened";
constexpr auto FL_ROI_CALIBRATED = "ROIcalibrated";
constexpr auto FL_BASE_PLANE_COMPUTED = "basePlaneComputed";
constexpr auto FL_IS_CALIBRATED = "projKinectCalibrated";
constexpr auto FL_DRAW_CONTOUR_LINES = "drawContourLines";
constexpr auto FL_CONTOUR_LINE_DISTANCE = "contourLineDistance";
constexpr auto FL_COLOR_MAP_FILE = "colorMapFile";

constexpr auto FL_DRAW_KINECT_DEPTH_VIEW = "drawKinectDepthView";
constexpr auto FL_DRAW_KINECT_COLOR_VIEW = "drawKinectColorView";
constexpr auto FL_DUMP_DEBUG_FILES = "dumpDebugFiles";
constexpr auto FL_CEILING = "ceiling";
constexpr auto FL_SPATIAL_FILTERING = "spatialFiltering";
constexpr auto FL_DO_INPAINTING = "doInpainting";
constexpr auto FL_DO_FULL_FRAME_FILTERING = "doFullFrameFiltering";
constexpr auto FL_QUICK_REACTION = "quickReaction";
constexpr auto FL_AVERAGING = "averaging";
constexpr auto FL_TILT_X = "tiltX";
constexpr auto FL_TILT_Y = "tiltY";
constexpr auto FL_VERTICAL_OFFSET = "verticalOffset";
constexpr auto FL_DO_SHOW_ROI_ON_PROJECTOR = "doShowROIonProjector";
constexpr auto FL_KINECT_ROI = "kinectROI";

constexpr auto FL_OF_FISH = "ofFish";
constexpr auto FL_OF_SHARKS = "ofSharks";
constexpr auto FL_OF_RABBITS = "ofRabbits";
constexpr auto FL_SHOW_MOTHER_FISH = "showMotherFish";
constexpr auto FL_SHOW_MOTHER_RABBIT = "showMotherRabbit";
constexpr auto FL_DO_FLIPPED_DRAWING = "doFlippedDrawing";

constexpr auto FL_FIELD = "field";
constexpr auto FL_VALUE = "value";
constexpr auto FL_RESULT = "result";
constexpr auto FL_ERROR = "error";
constexpr auto FL_MESSAGE = "message";

constexpr auto FL_KINECT_COLOR_IMAGE = "kinectColorImage";

// commands
constexpr auto CM_GET_STATE = "getState";
constexpr auto CM_SET_VALUE = "setValue";
constexpr auto CM_SET_STATE = "setState";
constexpr auto CM_GET_VALUE = "getValue";

constexpr auto CM_OP_START = "start";
constexpr auto CM_OP_START_CALIB = "startCalibration";
constexpr auto CM_OP_CANCEL_CALIB = "cancelCalibration";
constexpr auto CM_OP_CONTINUE_CALIB = "continueCalibration";
constexpr auto CM_OP_RESET_SEA_LEVEL = "resetSeaLevel";
constexpr auto CM_OP_CLEAR_ANIMALS = "clearAnimals";

/*constexpr auto CM_SET_DRAW_KINECT_DEPTH_VIEW = "setDrawKinectDepthView";
constexpr auto CM_SET_DRAW_KINECT_COLOR_VIEW = "setDrawKinectColorView";
constexpr auto CM_SET_DUMP_DEBUG_FILES = "setDumpDebugFiles";
constexpr auto CM_SET_SPATIAL_FILTERING = "setSpatialFiltering";
constexpr auto CM_SET_DO_INPAINTING = "setDoInpainting";
constexpr auto CM_SET_CEILING = "setCeiling";
constexpr auto CM_SET_AVERAGING = "setAveraging";
constexpr auto CM_SET_TILTX = "setTiltX";
constexpr auto CM_SET_TILTY = "setTiltY";
constexpr auto CM_SET_VERTICAL_OFFSET = "setVerticalOffset";
constexpr auto CM_SET_DO_FULL_FRAME_FILTERING = "setDoFullFrameFiltering";
constexpr auto CM_SET_QUICK_REACTION = "setQuickReaction";
constexpr auto CM_SET_DO_SHOW_ROI_ON_PROJECTOR = "setDowShowRoiOnProjector";
constexpr auto CM_SET_OF_FISH = "setOfFish";
constexpr auto CM_SET_OF_SHARK = "setOfShark";
constexpr auto CM_SET_OF_RABBITS = "setOfRabbits";*/

class WebsocketServer {
public:
    WebsocketServer(std::shared_ptr<KinectProjector> const& kp, CBoidGameController* const boidGameController, SandSurfaceRenderer* sandSurfaceRenderer);
    
    ofxLibwebsockets::Server server;
    bool bSetup;

    
    //string to send to clients
    string toSend;

    void broadcastState();

    // websocket methods
    void onConnect(ofxLibwebsockets::Event& args);
    void onOpen(ofxLibwebsockets::Event& args);
    void onClose(ofxLibwebsockets::Event& args);
    void onIdle(ofxLibwebsockets::Event& args);
    void onMessage(ofxLibwebsockets::Event& args);
    void onBroadcast(ofxLibwebsockets::Event& args);
    void broadcast(Json::Value message);
    void broadcastError(string error);
    void resolveResponseBool(ofxLibwebsockets::Event& args, int result);
    void resolveResponseFloat(ofxLibwebsockets::Event& args, int result);
private: 
    std::shared_ptr<KinectProjector> kinectProjector;
    CBoidGameController* boidGameController;
    SandSurfaceRenderer* sandSurfaceRenderer;

    //void setToggleComponentValue(string name, bool value);
    //void setSliderComponentValue(string name, float value);
    Json::Value getStateMessage();
    void resolveGetState(ofxLibwebsockets::Event& args);
	void resolveSetState(ofxLibwebsockets::Event& args);
    void resolveGetValue(ofxLibwebsockets::Event& args);
    void resolveSetKinectROI(ofxLibwebsockets::Event& args);
    void resolveSetValue(ofxLibwebsockets::Event& args);
    void sendMessage(ofxLibwebsockets::Event& args, Json::Value message);
    void sendMessage(ofxLibwebsockets::Event& args, Json::Value message, bool noLog);
    void sendToConnection(ofxLibwebsockets::Connection& connection, Json::Value message, bool noLog);
    void sendToConnection(ofxLibwebsockets::Connection* connection, Json::Value message, bool noLog);
    template <typename Proc>
    void resolveToggleValue(ofxLibwebsockets::Event& args, string componentName, Proc method);
	template<typename Proc>
	void resolveStringValue(ofxLibwebsockets::Event& args, Proc method, string componentName);
//    template<typename Proc>
//    void resolveStringValue(ofxLibwebsockets::Event& args, Proc method, string componentName, o_fxDatGui* gui);
	template<typename Proc>
	void resolveFloatValue(ofxLibwebsockets::Event& args, Proc method, string componentName);
//    template<typename Proc>
//    void resolveFloatValue(ofxLibwebsockets::Event& args, Proc method, string componentName, o_fxDatGui* gui);
    template<typename T>
    void resolveResponse(ofxLibwebsockets::Event& args, int result, T p);
    template<typename T>
    void resolveResponseValue(ofxLibwebsockets::Event& args, T value, string error);
    void resolveResponseState(ofxLibwebsockets::Event& args, int result, string error);
};
