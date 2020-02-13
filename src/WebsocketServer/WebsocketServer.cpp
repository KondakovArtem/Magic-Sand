#include "WebsocketServer.h"



WebsocketServer::WebsocketServer(
	std::shared_ptr<KinectProjector> const& kp, 
	CBoidGameController const bgc
) {
	ofxLibwebsockets::ServerOptions options = ofxLibwebsockets::defaultServerOptions();
	options.port = 9092;
	options.bUseSSL = false; // you'll have to manually accept this self-signed cert if 'true'!
	bSetup = server.setup(options);
	// this adds your app as a listener for the server
	server.addListener(this);
	kinectProjector = kp;
	boidGameController = bgc;
}


//--------------------------------------------------------------
void WebsocketServer::onConnect(ofxLibwebsockets::Event& args) {
	cout << "on connected" << endl;
}

//--------------------------------------------------------------
void WebsocketServer::onOpen(ofxLibwebsockets::Event& args) {
	cout << "new connection open" << endl;
	//messages.push_back("New connection from " + args.conn.getClientIP() + ", " + args.conn.getClientName());
}

//--------------------------------------------------------------
void WebsocketServer::onClose(ofxLibwebsockets::Event& args) {
	cout << "on close" << endl;
	//messages.push_back("Connection closed");
}

//--------------------------------------------------------------
void WebsocketServer::onIdle(ofxLibwebsockets::Event& args) {
	cout << "on idle" << endl;
}

//--------------------------------------------------------------
void WebsocketServer::onMessage(ofxLibwebsockets::Event& args) {
	cout << "got message " << args.message << endl;

	// trace out string messages or JSON messages!
	if (!args.json.isNull()) {
		const auto command = args.json.get(FL_COMMAND, "defaultCommand").asString();

		(command == CM_SET_STATE) ? resolveSetState(args) :
		(command == CM_GET_STATE) ? resolveGetState(args) :
		(command == CM_SET_VALUE) ? resolveSetValue(args) :
		(command == CM_GET_VALUE) ? resolveGetValue(args) :
		0;
	} else {
		cout << "New message: " + args.message + " from " + args.conn.getClientName();
		args.conn.send(args.message);
	}
	// echo server = send message right back!
}

//--------------------------------------------------------------
void WebsocketServer::onBroadcast(ofxLibwebsockets::Event& args) {
	cout << "got broadcast " << args.message << endl;
}

void WebsocketServer::resolveResponseBool(ofxLibwebsockets::Event& args, int result) {
	bool value = args.json.get(FL_VALUE, false).asBool();
	resolveResponse<bool>(args, result, value);
}

void WebsocketServer::resolveResponseFloat(ofxLibwebsockets::Event& args, int result) {
	float value = args.json.get(FL_VALUE, false).asFloat();
	resolveResponse<float>(args, result, value);
}

template <typename T>
void WebsocketServer::resolveResponse(ofxLibwebsockets::Event& args, int result, T value) {
	Json::Value message;
	message[FL_COMMAND] = args.json.get(FL_COMMAND, "FL_COMMAND").asString();
	message[FL_FIELD] = args.json.get(FL_FIELD, "FL_FIELD").asString();
	message[FL_VALUE] = value;
	message[FL_RESULT] = result;
	sendMessage(args, message);
}

void WebsocketServer::resolveResponseState(ofxLibwebsockets::Event& args, int result, string error) {
	Json::Value message;
	message[FL_COMMAND] = args.json.get(FL_COMMAND, "FL_COMMAND").asString();
	message[FL_VALUE] = args.json.get(FL_VALUE, "FL_VALUE").asString();
	(!error.empty()) ? message[FL_ERROR] = error : 0;
	message[FL_RESULT] = result;
	sendMessage(args, message);
}

template <typename T>
void WebsocketServer::resolveResponseValue(ofxLibwebsockets::Event& args, T value, string error) {
	Json::Value message;
	message[FL_COMMAND] = args.json.get(FL_COMMAND, "FL_COMMAND").asString();
	message[FL_FIELD] = args.json.get(FL_FIELD, "FL_FIELD").asString();
	message[FL_VALUE] = value;
	(!error.empty()) ? message[FL_ERROR] = error : 0;
	sendMessage(args, message, true);
}

void WebsocketServer::resolveGetState(ofxLibwebsockets::Event& args) {
	Json::Value message;
	message[FL_COMMAND] = CM_GET_STATE;
	
	message[FL_APPLICATION_STATE] = kinectProjector->GetApplicationState();
	message[FL_CALIBRATION_STATE] = kinectProjector->GetCalibrationState();
	message[FL_AUTO_CALIBRATION_STATE] = kinectProjector->GetAutoCalibrationState();
	message[FL_DRAW_KINECT_DEPTH_VIEW] = kinectProjector->getDrawKinectDepthView();
	message[FL_DRAW_KINECT_COLOR_VIEW] = kinectProjector->getDrawKinectColorView();
	message[FL_DUMP_DEBUG_FILES] = kinectProjector->getDumpDebugFiles();
	message[FL_SPATIAL_FILTERING] = kinectProjector->getSpatialFiltering();
	message[FL_DO_INPAINTING] = kinectProjector->getInPainting();
	message[FL_DO_FULL_FRAME_FILTERING] = kinectProjector->getFullFrameFiltering();
	message[FL_QUICK_REACTION] = kinectProjector->getFollowBigChanges();
	message[FL_AVERAGING] = kinectProjector->getAveraging();
	message[FL_CEILING] = kinectProjector->getCeiling();
	message[FL_TILT_X] = kinectProjector->getTiltX();
	message[FL_TILT_Y] = kinectProjector->getTiltY();
	message[FL_VERTICAL_OFFSET] = kinectProjector->getVerticalOffset();
	message[FL_DO_SHOW_ROI_ON_PROJECTOR] = kinectProjector->getShowROIonProjector();
	
	Json::Value roi;
	roi["x"] = kinectProjector->getKinectROI().getX();
	roi["y"] = kinectProjector->getKinectROI().getY();
	roi["width"] = kinectProjector->getKinectROI().getWidth();
	roi["height"] = kinectProjector->getKinectROI().getHeight();
	message[FL_KINECT_ROI] = roi;

	message[FL_OF_SHARKS] = boidGameController.getSharks();
	message[FL_OF_FISH] = boidGameController.getFish();
	message[FL_OF_RABBITS] = boidGameController.getRabbits();

	sendMessage(args, message);
}


void WebsocketServer::resolveSetState(ofxLibwebsockets::Event& args) {
	const auto kp = this->kinectProjector;
	const auto value = args.json.get(FL_VALUE, "").asString();

	if (value == CM_OP_START) {
		string res = kp->startApplication(false);
		int result = (res.empty()) ? 0 : 1;
		resolveResponseState(args, result, res);
	} else
	if (value == CM_OP_START_CALIB) {
		string res = kp->startAutomaticKinectProjectorCalibration(false);
		int result = (res.empty()) ? 0 : 1;
		resolveResponseState(args, result, res);
	}
}


void WebsocketServer::resolveGetValue(ofxLibwebsockets::Event& args) {
	const auto kp = this->kinectProjector;
	const auto field = args.json.get(FL_FIELD, "").asString();
	string error;
	if (field == FL_KINECT_COLOR_IMAGE) {
		string image = kp->getKinectColorImage();
		int result = (image.empty()) ? 0 : 1;
		resolveResponseValue<string>(args, image, error);
	}
}


void WebsocketServer::resolveSetKinectROI(ofxLibwebsockets::Event& args) {
	const auto kp = this->kinectProjector;
	const auto roi = args.json.get(FL_VALUE, "");
	kp->setKinectROI(
		roi["x"].asInt(), 
		roi["y"].asInt(), 
		roi["width"].asInt(), 
		roi["height"].asInt()
	);
}


void WebsocketServer::resolveSetValue(ofxLibwebsockets::Event& args) {

	const auto field = args.json.get(FL_FIELD, "").asString();
	const auto kp = this->kinectProjector;
	const auto getBoidGui = [this]() {return kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING ? this->boidGameController.getGui() : nullptr; };
	const auto getGui = [this]() {return this->kinectProjector->getGui(); };
	
	(field == FL_DRAW_KINECT_DEPTH_VIEW) ? resolveToggleValue(args, CMP_DRAW_KINECT_DEPTH_VIEW, [kp](bool val) { kp->setDrawKinectDepthView(val); }) :
	(field == FL_DRAW_KINECT_COLOR_VIEW) ? resolveToggleValue(args, CMP_DRAW_KINECT_COLOR_VIEW, [kp](bool val) { kp->setDrawKinectColorView(val); }) :
	(field == FL_DUMP_DEBUG_FILES) ? resolveToggleValue(args, CMP_DUMP_DEBUG, [kp](bool val) { kp->setDumpDebugFiles(val); }) :
	(field == FL_CEILING) ? resolveFloatValue(args, [kp](float val) { kp->setCeiling(val); }, CMP_CEILING, getGui()) :
	(field == FL_SPATIAL_FILTERING) ? resolveToggleValue(args, CMP_SPATIAL_FILTERING, [kp](bool val) { kp->setSpatialFiltering(val, false); }) :
	(field == FL_DO_INPAINTING) ? resolveToggleValue(args, CMP_INPAINT_OUTLIERS, [kp](bool val) { kp->setInPainting(val, false); }) :
	(field == FL_DO_FULL_FRAME_FILTERING) ? resolveToggleValue(args, CMP_FULL_FRAME_FILTERING, [kp](bool val) { kp->setFullFrameFiltering(val, false); }) :
	(field == FL_QUICK_REACTION) ? resolveToggleValue(args, CMP_QUICK_REACTION, [kp](bool val) { kp->setFollowBigChanges(val, false); }) :
	(field == FL_AVERAGING) ? resolveFloatValue(args, [kp](float val) { kp->setAveraging(val); }, CMP_AVERAGING, getGui()) :
	(field == FL_TILT_X) ? resolveFloatValue(args, [kp](float val) { kp->setTiltX(val); }, CMP_TILT_X, getGui()) :
	(field == FL_TILT_Y) ? resolveFloatValue(args, [kp](float val) { kp->setTiltY(val); }, CMP_TILT_Y, getGui()) :
	(field == FL_VERTICAL_OFFSET) ? resolveFloatValue(args, [kp](float val) { kp->setVerticalOffset(val); }, CMP_VERTICAL_OFFSET, getGui()) :
	(field == FL_DO_SHOW_ROI_ON_PROJECTOR) ? resolveToggleValue(args, CMP_SHOW_ROI_ON_SAND, [kp](bool val) { kp->showROIonProjector(val); }) :
	(field == FL_KINECT_ROI) ? resolveSetKinectROI(args) :

	(field == FL_OF_FISH) ? resolveFloatValue(args, [this](float val) { this->boidGameController.setFish(val); }, CMP_OF_FISH, getBoidGui() ) :
	(field == FL_OF_SHARKS) ? resolveFloatValue(args, [this](float val) { this->boidGameController.setSharks(val); }, CMP_OF_SHARKS, getBoidGui()) :
	(field == FL_OF_RABBITS) ? resolveFloatValue(args, [this](float val) { this->boidGameController.setRabbits(val); }, CMP_OF_RABBITS, getBoidGui()) :
	kp->externUpdate = true;
}


template <typename Proc>
void WebsocketServer::resolveToggleValue(ofxLibwebsockets::Event& args, string componentName, Proc method) {
	bool value = args.json.get(FL_VALUE, false).asBool();
	method(value);
	kinectProjector->setForceGuiUpdate(true);
	resolveResponseBool(args, 0);
}

template <typename Proc>
void WebsocketServer::resolveFloatValue(ofxLibwebsockets::Event& args, Proc method, string componentName, ofxDatGui* gui) {
	float value = args.json.get(FL_VALUE, 0).asFloat();
	method(value);
	if (gui != nullptr) {
		auto slider = gui->getSlider(componentName);
		kinectProjector->setForceGuiUpdate(true);
	}
	resolveResponseFloat(args, 0);
}


void WebsocketServer::sendMessage(ofxLibwebsockets::Event& args, Json::Value message) {
	sendMessage(args, message, false);
}

void WebsocketServer::sendMessage(ofxLibwebsockets::Event& args, Json::Value message, bool noLog) {
	Json::StyledWriter writer;
	string str = writer.write(message);
	if (!noLog) {
		cout << "send message " << str << endl;
	}
	else {
		cout << "send message " << message[FL_COMMAND].asString() << endl;
	}
	args.conn.send(str);
}