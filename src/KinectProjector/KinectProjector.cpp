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
#define noop ((void)0)

#include "KinectProjector.h"
#include <sstream>

using namespace ofxCSG;

KinectProjector::KinectProjector(std::shared_ptr<ofAppBaseWindow> const &p)
	: ROIcalibrated(false),
	  projKinectCalibrated(false),
	  //calibrating (false),
	  basePlaneUpdated(false),
	  basePlaneComputed(false),
	  projKinectCalibrationUpdated(false),
	  //ROIUpdated (false),
	  imageStabilized(false),
	  waitingForFlattenSand(false),
	  drawKinectView(false),
	  drawKinectColorView(true)
{
	doShowROIonProjector = false;
	tiltX = 0;
	tiltY = 0;
	applicationState = APPLICATION_STATE_SETUP;
	confirmModalState = CONFIRM_MODAL_CLOSED;
	confirmModalMessage = "";
	projWindow = p;
	TemporalFilteringType = 1;
	verticalOffset = 0;
	DumpDebugFiles = true;
	DebugFileOutDir = "DebugFiles//";
	forceGuiUpdate = false;
	askToFlattenSandFlag = false;
}

void KinectProjector::setup(bool sdisplayGui)
{
	setApplicationState(APPLICATION_STATE_SETUP);
	
	ofAddListener(ofEvents().exit, this, &KinectProjector::exit);

	// instantiate the modal windows //
	modalTheme = make_shared<ofxModalThemeProjKinect>();
	confirmModal = make_shared<ofxModalConfirm>();
	confirmModal->setTheme(modalTheme);
	confirmModal->addListener(this, &KinectProjector::onConfirmModalEvent);
	confirmModal->setButtonLabel("Ok");

	calibModal = make_shared<ofxModalAlert>();
	calibModal->setTheme(modalTheme);
	calibModal->addListener(this, &KinectProjector::onCalibModalEvent);
	calibModal->setButtonLabel("Cancel");

	displayGui = sdisplayGui;

	// calibration chessboard config
	chessboardSize = 300;
	chessboardX = 5;
	chessboardY = 4;

	// 	Gradient Field
	gradFieldResolution = 10;
	arrowLength = 25;

	// Setup default base plane
	basePlaneNormalBack = ofVec3f(0, 0, 1);   // This is our default baseplane normal
	basePlaneOffsetBack = ofVec3f(0, 0, 870); // This is our default baseplane offset
	basePlaneNormal = basePlaneNormalBack;
	basePlaneOffset = basePlaneOffsetBack;
	basePlaneEq = getPlaneEquation(basePlaneOffset, basePlaneNormal);
	maxOffsetBack = basePlaneOffset.z - 300;
	maxOffset = maxOffsetBack;
	maxOffsetSafeRange = 50; // Range above the autocalib measured max offset

	// kinectgrabber: start & default setup
	kinectOpened = kinectgrabber.setup();
	lastKinectOpenTry = ofGetElapsedTimef();
	if (!kinectOpened)
	{
		// If the kinect is not found and opened (which happens very often on Windows 10) then just go with default values for the Kinect
		ofLogVerbose("KinectProjector") << "KinectProjector.setup(): Kinect not found - trying again later";
	}

	doInpainting = false;
	doFullFrameFiltering = false;
	spatialFiltering = true;
	followBigChanges = false;
	numAveragingSlots = 15;
	TemporalFrameCounter = 0;

	// Get projector and kinect width & height
	projRes = ofVec2f(projWindow->getWidth(), projWindow->getHeight());
	kinectRes = kinectgrabber.getKinectSize();
	kinectROI = ofRectangle(0, 0, kinectRes.x, kinectRes.y);
	ofLogVerbose("KinectProjector") << "KinectProjector.setup(): kinectROI " << kinectROI;

	// Initialize the fbos and images
	FilteredDepthImage.allocate(kinectRes.x, kinectRes.y);
	kinectColorImage.allocate(kinectRes.x, kinectRes.y);
	thresholdedImage.allocate(kinectRes.x, kinectRes.y);

	kpt = new ofxKinectProjectorToolkit(projRes, kinectRes);

	// finish kinectgrabber setup and start the grabber
	kinectgrabber.setupFramefilter(gradFieldResolution, maxOffset, kinectROI, spatialFiltering, followBigChanges, numAveragingSlots);
	kinectWorldMatrix = kinectgrabber.getWorldMatrix();
	ofLogVerbose("KinectProjector") << "KinectProjector.setup(): kinectWorldMatrix: " << kinectWorldMatrix;

	// Setup gradient field
	setupGradientField();

	fboProjWindow.allocate(projRes.x, projRes.y, GL_RGBA);
	fboProjWindow.begin();
	ofClear(255, 255, 255, 0);
	ofBackground(255); // Set to white in setup mode
	fboProjWindow.end();

	fboMainWindow.allocate(kinectRes.x, kinectRes.y, GL_RGBA);
	fboMainWindow.begin();
	ofClear(255, 255, 255, 0);
	fboMainWindow.end();

	if (displayGui)
		setupGui();

	kinectgrabber.start(); // Start the acquisition

	updateStatusGUI();
	checkStartReady(false);
}

void KinectProjector::exit(ofEventArgs &e)
{
	if (ROIcalibrated)
	{
		if (saveSettings())
		{
			ofLogVerbose("KinectProjector") << "exit(): Settings saved ";
		}
		else
		{
			ofLogVerbose("KinectProjector") << "exit(): Settings could not be saved ";
		}
	}
}

void KinectProjector::setupGradientField()
{
	gradFieldcols = kinectRes.x / gradFieldResolution;
	gradFieldrows = kinectRes.y / gradFieldResolution;

	gradField = new ofVec2f[gradFieldcols * gradFieldrows];
	ofVec2f *gfPtr = gradField;
	for (unsigned int y = 0; y < gradFieldrows; ++y)
		for (unsigned int x = 0; x < gradFieldcols; ++x, ++gfPtr)
			*gfPtr = ofVec2f(0);
}

void KinectProjector::setGradFieldResolution(int sgradFieldResolution)
{
	gradFieldResolution = sgradFieldResolution;
	setupGradientField();
	kinectgrabber.performInThread([sgradFieldResolution](KinectGrabber &kg) {
		kg.setGradFieldResolution(sgradFieldResolution);
	});
}

// For some reason this call eats milliseconds - so it should only be called when something is changed
// else it would be convenient just to call it in every update
void KinectProjector::updateStatusGUI()
{
	if (kinectOpened)
	{
		StatusGUI->getLabel("Kinect Status")->setLabel("Kinect running");
		StatusGUI->getLabel("Kinect Status")->setLabelColor(ofColor(0, 255, 0));
	}
	else
	{
		StatusGUI->getLabel("Kinect Status")->setLabel("Kinect not found");
		StatusGUI->getLabel("Kinect Status")->setLabelColor(ofColor(255, 0, 0));
	}

	if (ROIcalibrated)
	{
		StatusGUI->getLabel("ROI Status")->setLabel("ROI defined");
		StatusGUI->getLabel("ROI Status")->setLabelColor(ofColor(0, 255, 0));
	}
	else
	{
		StatusGUI->getLabel("ROI Status")->setLabel("ROI not defined");
		StatusGUI->getLabel("ROI Status")->setLabelColor(ofColor(255, 0, 0));
	}

	if (basePlaneComputed)
	{
		StatusGUI->getLabel("Baseplane Status")->setLabel("Baseplane found");
		StatusGUI->getLabel("Baseplane Status")->setLabelColor(ofColor(0, 255, 0));
	}
	else
	{
		StatusGUI->getLabel("Baseplane Status")->setLabel("Baseplane not found");
		StatusGUI->getLabel("Baseplane Status")->setLabelColor(ofColor(255, 0, 0));
	}

	if (isCalibrated())
	{
		StatusGUI->getLabel("Calibration Status")->setLabel("Projector/Kinect calibrated");
		StatusGUI->getLabel("Calibration Status")->setLabelColor(ofColor(0, 255, 0));
	}
	else
	{
		StatusGUI->getLabel("Calibration Status")->setLabel("Projector/Kinect not calibrated");
		StatusGUI->getLabel("Calibration Status")->setLabelColor(ofColor(255, 0, 0));
	}

	StatusGUI->getLabel("Projector Status")->setLabel("Projector " + ofToString(projRes.x) + " x " + ofToString(projRes.y));

	std::string AppStatus = "Setup";
	if (GetApplicationState() == APPLICATION_STATE_CALIBRATING)
		AppStatus = "Calibrating";
	else if (GetApplicationState() == APPLICATION_STATE_RUNNING)
		AppStatus = "Running";

	StatusGUI->getLabel("Application Status")->setLabel("Application state: " + AppStatus);
	StatusGUI->getLabel("Application Status")->setLabelColor(ofColor(255, 255, 0));

	StatusGUI->getLabel("Calibration Step")->setLabel("Calibration Step: " + calibrationText);
	;
	StatusGUI->getLabel("Calibration Step")->setLabelColor(ofColor(0, 255, 255));

	gui->getToggle(CMP_SPATIAL_FILTERING)->setChecked(spatialFiltering);
	gui->getToggle(CMP_QUICK_REACTION)->setChecked(followBigChanges);
	gui->getToggle(CMP_INPAINT_OUTLIERS)->setChecked(doInpainting);
	gui->getToggle(CMP_FULL_FRAME_FILTERING)->setChecked(doFullFrameFiltering);
}

void KinectProjector::setForceGuiUpdate(bool value)
{
	forceGuiUpdate = value;
}

void KinectProjector::updateGuiValue()
{
	if (forceGuiUpdate)
	{
		gui->getToggle(CMP_DRAW_KINECT_DEPTH_VIEW)->setChecked(drawKinectView);
		gui->getToggle(CMP_DRAW_KINECT_COLOR_VIEW)->setChecked(drawKinectColorView);
		gui->getToggle(CMP_DUMP_DEBUG)->setChecked(DumpDebugFiles);
		gui->getSlider(CMP_CEILING)->setValue(getMaxOffset());
		gui->getToggle(CMP_SPATIAL_FILTERING)->setChecked(spatialFiltering);
		gui->getToggle(CMP_INPAINT_OUTLIERS)->setChecked(doInpainting);
		gui->getToggle(CMP_FULL_FRAME_FILTERING)->setChecked(doFullFrameFiltering);
		gui->getToggle(CMP_QUICK_REACTION)->setChecked(followBigChanges);
		gui->getSlider(CMP_AVERAGING)->setValue(numAveragingSlots);
		gui->getSlider(CMP_TILT_X)->setValue(tiltX);
		gui->getSlider(CMP_TILT_Y)->setValue(tiltY);
		gui->getSlider(CMP_VERTICAL_OFFSET)->setValue(verticalOffset);
		setForceGuiUpdate(false);
	}
}

void KinectProjector::update()
{
	// Clear updated state variables
	basePlaneUpdated = false;
	//    ROIUpdated = false;
	projKinectCalibrationUpdated = false;

	// Try to open the kinect every 3. second if it is not yet open
	float TimeStamp = ofGetElapsedTimef();
	if (!kinectOpened && TimeStamp - lastKinectOpenTry > 3)
	{
		lastKinectOpenTry = TimeStamp;
		kinectOpened = kinectgrabber.openKinect();

		if (kinectOpened)
		{
			ofLogVerbose("KinectProjector") << "KinectProjector.update(): A Kinect was found ";
			kinectRes = kinectgrabber.getKinectSize();
			kinectROI = ofRectangle(0, 0, kinectRes.x, kinectRes.y);
			ofLogVerbose("KinectProjector") << "KinectProjector.update(): kinectROI " << kinectROI;

			kinectgrabber.setupFramefilter(gradFieldResolution, maxOffset, kinectROI, spatialFiltering, followBigChanges, numAveragingSlots);
			kinectWorldMatrix = kinectgrabber.getWorldMatrix();
			ofLogVerbose("KinectProjector") << "KinectProjector.update(): kinectWorldMatrix: " << kinectWorldMatrix;

			updateStatusGUI();
		}
	}

	if (askToFlattenSandFlag)
	{
		askToFlattenSandFlag = false;
		askToFlattenSand();
	}

	if (displayGui)
	{

		updateGuiValue();
		gui->update();
		StatusGUI->update();
	}

	// Get images from kinect grabber
	ofFloatPixels filteredframe;
	if (kinectOpened && kinectgrabber.filtered.tryReceive(filteredframe))
	{
		fpsKinect.newFrame();
		fpsKinectText->setText(ofToString(fpsKinect.getFps(), 2));

		FilteredDepthImage.setFromPixels(filteredframe.getData(), kinectRes.x, kinectRes.y);
		FilteredDepthImage.updateTexture();

		// Get color image from kinect grabber
		ofPixels coloredframe;
		if (kinectgrabber.colored.tryReceive(coloredframe))
		{
			kinectColorImage.setFromPixels(coloredframe);

			if (TemporalFilteringType == 0)
				TemporalFrameFilter.NewFrame(kinectColorImage.getPixels().getData(), kinectColorImage.width, kinectColorImage.height);
			else if (TemporalFilteringType == 1)
				TemporalFrameFilter.NewColFrame(kinectColorImage.getPixels().getData(), kinectColorImage.width, kinectColorImage.height);
		}

		// Get gradient field from kinect grabber
		kinectgrabber.gradient.tryReceive(gradField);

		// Update grabber stored frame number
		kinectgrabber.lock();
		kinectgrabber.decStoredframes();
		kinectgrabber.unlock();

		// Is the depth image stabilized
		imageStabilized = kinectgrabber.isImageStabilized();

		// Are we calibrating ?
		if (GetApplicationState() == APPLICATION_STATE_CALIBRATING && !waitingForFlattenSand)
		{
			updateCalibration();
		}
		else
		{
			//ofEnableAlphaBlending();
			fboMainWindow.begin();
			if (drawKinectView || drawKinectColorView)
			{
				if (drawKinectColorView)
				{
					kinectColorImage.updateTexture();
					kinectColorImage.draw(0, 0);
				}
				else
				{
					FilteredDepthImage.draw(0, 0);
				}
				ofNoFill();

				if (ROIcalibrated)
				{
					ofSetColor(0, 0, 255);
					ofDrawRectangle(kinectROI);
				}

				ofSetColor(255, 0, 0);
				ofDrawRectangle(1, 1, kinectRes.x - 1, kinectRes.y - 1);

				if (GetCalibrationState() == CALIBRATION_STATE_ROI_MANUAL_DETERMINATION && GetROICalibState() == ROI_CALIBRATION_STATE_INIT)
				{
					int xmin = std::min((int)ROIStartPoint.x, (int)ROICurrentPoint.x);
					int xmax = std::max((int)ROIStartPoint.x, (int)ROICurrentPoint.x);
					int ymin = std::min((int)ROIStartPoint.y, (int)ROICurrentPoint.y);
					int ymax = std::max((int)ROIStartPoint.y, (int)ROICurrentPoint.y);

					if (xmin >= 0) // Start point has been set
					{
						ofSetColor(0, 255, 0);
						ofRectangle tempRect(xmin, ymin, xmax - xmin, ymax - ymin);
						ofDrawRectangle(tempRect);
					}
				}
			}
			else
			{
				ofClear(0, 0, 0, 0);
			}
			fboMainWindow.end();
		}
	}

	fboProjWindow.begin();

	if (GetApplicationState() != APPLICATION_STATE_CALIBRATING)
	{
		ofClear(255, 255, 255, 0);
	}
	if (doShowROIonProjector && ROIcalibrated && kinectOpened)
	{
		ofNoFill();
		ofSetLineWidth(4);

		// Draw rectangle of ROI using the offset by the current sand level
		ofVec2f UL = kinectCoordToProjCoord(kinectROI.getMinX(), kinectROI.getMinY());
		ofVec2f LR = kinectCoordToProjCoord(kinectROI.getMaxX() - 1, kinectROI.getMaxY() - 1);

		// projector resolution
		ofVec2f pR = ofVec2f(projWindow->getWidth(), projWindow->getHeight());
		// kinect resolution
		ofVec2f kR = ofVec2f(kinectRes.x, kinectRes.y);

		float adjX = pR.x / kR.x;
		float adjY = pR.y / kR.y;

		ofSetColor(255, 255, 0);
		ofDrawRectangle(kinectROI.getMinX() * adjX, kinectROI.getMinY() * adjY, kinectROI.getWidth() * adjX, kinectROI.getHeight() * adjY);

		ofSetColor(255, 0, 0);
		ofRectangle tempRect(ofPoint(UL.x, UL.y), ofPoint(LR.x, LR.y));
		ofDrawRectangle(tempRect);

		ofSetColor(0, 0, 255);
		ofRectangle tempRect2(ofPoint(UL.x - 2, UL.y - 2), ofPoint(UL.x + 2, UL.y + 2));
		ofDrawRectangle(tempRect2);

		// Draw rectangle of ROI using the offset by the waterlevel
		UL = kinectCoordToProjCoord(kinectROI.getMinX(), kinectROI.getMinY(), basePlaneOffset.z);
		LR = kinectCoordToProjCoord(kinectROI.getMaxX(), kinectROI.getMaxY(), basePlaneOffset.z);

		ofSetColor(0, 255, 0);
		tempRect = ofRectangle(ofPoint(UL.x, UL.y), ofPoint(LR.x, LR.y));
		ofDrawRectangle(tempRect);

		ofSetColor(255, 0, 255);
		tempRect2 = ofRectangle(ofPoint(UL.x - 2, UL.y - 2), ofPoint(UL.x + 2, UL.y + 2));
		ofDrawRectangle(tempRect2);
	}
	else if (GetApplicationState() == APPLICATION_STATE_SETUP)
	{
		ofBackground(255); // Set to white in setup mode
	}
	fboProjWindow.end();
}

void KinectProjector::mousePressed(int x, int y, int button)
{
	if (GetCalibrationState() == CALIBRATION_STATE_ROI_MANUAL_DETERMINATION && GetROICalibState() == ROI_CALIBRATION_STATE_INIT)
	{
		ROIStartPoint.x = x;
		ROIStartPoint.y = y;
		ROICurrentPoint.x = x;
		ROICurrentPoint.y = y;
	}
	else if (kinectOpened && drawKinectView)
	{
		int ind = y * kinectRes.x + x;
		if (ind >= 0 && ind < FilteredDepthImage.getFloatPixelsRef().getTotalBytes())
		{
			float z = FilteredDepthImage.getFloatPixelsRef().getData()[ind];
			std::cout << "Kinect depth (x, y, z) = (" << x << ", " << y << ", " << z << ")" << std::endl;
		}
	}
}

void KinectProjector::mouseReleased(int x, int y, int button)
{
	if (GetCalibrationState() == CALIBRATION_STATE_ROI_MANUAL_DETERMINATION && GetROICalibState() == ROI_CALIBRATION_STATE_INIT)
	{
		if (ROIStartPoint.x >= 0)
		{
			x = std::max(0, x);
			x = std::min((int)kinectRes.x - 1, x);
			y = std::max(0, y);
			y = std::min((int)kinectRes.y - 1, y);

			ROICurrentPoint.x = x;
			ROICurrentPoint.y = y;

			int xmin = std::min((int)ROIStartPoint.x, (int)ROICurrentPoint.x);
			int xmax = std::max((int)ROIStartPoint.x, (int)ROICurrentPoint.x);
			int ymin = std::min((int)ROIStartPoint.y, (int)ROICurrentPoint.y);
			int ymax = std::max((int)ROIStartPoint.y, (int)ROICurrentPoint.y);

			ofRectangle tempRect(xmin, ymin, xmax - xmin, ymax - ymin);
			kinectROI = tempRect;
			setNewKinectROI();
			setROICalibState(ROI_CALIBRATION_STATE_DONE);
			calibrationText = "Manual ROI defined";
			updateStatusGUI();
		}
	}
}

void KinectProjector::mouseDragged(int x, int y, int button)
{
	if (GetCalibrationState() == CALIBRATION_STATE_ROI_MANUAL_DETERMINATION && GetROICalibState() == ROI_CALIBRATION_STATE_INIT)
	{
		x = std::max(0, x);
		x = std::min((int)kinectRes.x - 1, x);
		y = std::max(0, y);
		y = std::min((int)kinectRes.y - 1, y);

		ROICurrentPoint.x = x;
		ROICurrentPoint.y = y;
	}
}

bool KinectProjector::getProjectionFlipped()
{
	return (kinectProjMatrix(0, 0) < 0);
}

void KinectProjector::updateCalibration()
{
	
	if (GetCalibrationState() == CALIBRATION_STATE_FULL_AUTO_CALIBRATION)
	{
		std::cout << "updateCalibration calibrationState = "<< GetCalibrationState() << std::endl;
		updateFullAutoCalibration();
	}
	else if (GetCalibrationState() == CALIBRATION_STATE_ROI_AUTO_DETERMINATION)
	{
		std::cout << "updateCalibration calibrationState = "<< GetCalibrationState() << std::endl;
		updateROIAutoCalibration();
	}
	//else if (calibrationState == CALIBRATION_STATE_ROI_MANUAL_DETERMINATION)
	//{
	//       updateROIManualCalibration();
	//   }
	else if (GetCalibrationState() == CALIBRATION_STATE_PROJ_KINECT_AUTO_CALIBRATION)
	{
		//std::cout << "updateCalibration calibrationState = "<< calibrationState << std::endl;
		updateProjKinectAutoCalibration();
	}
	else if (GetCalibrationState() == CALIBRATION_STATE_PROJ_KINECT_MANUAL_CALIBRATION)
	{
		std::cout << "updateCalibration calibrationState = "<< GetCalibrationState() << std::endl;
		updateProjKinectManualCalibration();
	}
}

void KinectProjector::updateFullAutoCalibration()
{
	if (GetFullCalibState() == FULL_CALIBRATION_STATE_ROI_DETERMINATION)
	{
		//        updateROIAutoCalibration();
		updateROIFromFile();
		if (GetROICalibState() == ROI_CALIBRATION_STATE_DONE)
		{
			setFullCalibState(FULL_CALIBRATION_STATE_AUTOCALIB);
			setAutoCalibrationState(AUTOCALIB_STATE_INIT_FIRST_PLANE);
		}
	}
	else if (GetFullCalibState() == FULL_CALIBRATION_STATE_AUTOCALIB)
	{
		updateProjKinectAutoCalibration();
		if (GetAutoCalibrationState() == AUTOCALIB_STATE_DONE)
		{
			setFullCalibState(FULL_CALIBRATION_STATE_DONE);
		}
	}
}

void KinectProjector::updateROIAutoCalibration()
{
	//updateROIFromColorImage();
	updateROIFromDepthImage();
}

void KinectProjector::updateROIFromCalibration()
{
	ofVec2f a = worldCoordTokinectCoord(projCoordAndWorldZToWorldCoord(0, 0, basePlaneOffset.z));
	ofVec2f b = worldCoordTokinectCoord(projCoordAndWorldZToWorldCoord(projRes.x, 0, basePlaneOffset.z));
	ofVec2f c = worldCoordTokinectCoord(projCoordAndWorldZToWorldCoord(projRes.x, projRes.y, basePlaneOffset.z));
	ofVec2f d = worldCoordTokinectCoord(projCoordAndWorldZToWorldCoord(0, projRes.y, basePlaneOffset.z));
	float x1 = max(a.x, d.x);
	float x2 = min(b.x, c.x);
	float y1 = max(a.y, b.y);
	float y2 = min(c.y, d.y);
	ofRectangle smallKinectROI = ofRectangle(ofPoint(max(x1, kinectROI.getLeft()), max(y1, kinectROI.getTop())), ofPoint(min(x2, kinectROI.getRight()), min(y2, kinectROI.getBottom())));
	kinectROI = smallKinectROI;

	kinectROI.standardize();
	ofLogVerbose("KinectProjector") << "updateROIFromCalibration(): final kinectROI : " << kinectROI;
	setNewKinectROI();
}

//TODO: update color image ROI acquisition to use calibration modal
void KinectProjector::updateROIFromColorImage()
{
	fboProjWindow.begin();
	ofBackground(255);
	fboProjWindow.end();
	if (GetROICalibState() == ROI_CALIBRATION_STATE_INIT)
	{ // set kinect to max depth range
		setROICalibState(ROI_CALIBRATION_STATE_MOVE_UP);
		large = ofPolyline();
		threshold = 90;
	}
	else if (GetROICalibState() == ROI_CALIBRATION_STATE_MOVE_UP)
	{
		while (threshold < 255)
		{
			kinectColorImage.setROI(0, 0, kinectRes.x, kinectRes.y);
			thresholdedImage = kinectColorImage;
			cvThreshold(thresholdedImage.getCvImage(), thresholdedImage.getCvImage(), threshold, 255, CV_THRESH_BINARY_INV);
			contourFinder.findContours(thresholdedImage, 12, kinectRes.x * kinectRes.y, 5, true);
			ofPolyline small = ofPolyline();
			for (int i = 0; i < contourFinder.nBlobs; i++)
			{
				ofxCvBlob blobContour = contourFinder.blobs[i];
				if (blobContour.hole)
				{
					ofPolyline poly = ofPolyline(blobContour.pts);
					if (poly.inside(kinectRes.x / 2, kinectRes.y / 2))
					{
						if (small.size() == 0 || poly.getArea() < small.getArea())
						{
							small = poly;
						}
					}
				}
			}
			ofLogVerbose("KinectProjector") << "KinectProjector.updateROIFromColorImage(): small.getArea(): " << small.getArea();
			ofLogVerbose("KinectProjector") << "KinectProjector.updateROIFromColorImage(): large.getArea(): " << large.getArea();
			if (large.getArea() < small.getArea())
			{
				ofLogVerbose("KinectProjector") << "updateROIFromColorImage(): We take the largest contour line surroundings the center of the screen at all threshold level";
				large = small;
			}
			threshold += 1;
		}
		kinectROI = large.getBoundingBox();
		kinectROI.standardize();
		ofLogVerbose("KinectProjector") << "updateROIFromColorImage(): kinectROI : " << kinectROI;
		setROICalibState(ROI_CALIBRATION_STATE_DONE);
		setNewKinectROI();
	}
	else if (GetROICalibState() == ROI_CALIBRATION_STATE_DONE)
	{
	}
}

void KinectProjector::updateROIFromDepthImage()
{
	int counter = 0;
	if (GetROICalibState() == ROI_CALIBRATION_STATE_INIT)
	{
		calibModal->setMessage("Enlarging acquisition area & resetting buffers.");
		setMaxKinectGrabberROI();
		calibModal->setMessage("Stabilizing acquisition.");
		setROICalibState(ROI_CALIBRATION_STATE_READY_TO_MOVE_UP);
	}
	else if (GetROICalibState() == ROI_CALIBRATION_STATE_READY_TO_MOVE_UP && imageStabilized)
	{
		calibModal->setMessage("Scanning depth field to find sandbox walls.");
		ofLogVerbose("KinectProjector") << "updateROIFromDepthImage(): ROI_CALIBRATION_STATE_READY_TO_MOVE_UP: got a stable depth image";
		setROICalibState(ROI_CALIBRATION_STATE_MOVE_UP);
		large = ofPolyline();
		ofxCvFloatImage temp;
		temp.setFromPixels(FilteredDepthImage.getFloatPixelsRef().getData(), kinectRes.x, kinectRes.y);
		temp.setNativeScale(FilteredDepthImage.getNativeScaleMin(), FilteredDepthImage.getNativeScaleMax());
		temp.convertToRange(0, 1);
		thresholdedImage.setFromPixels(temp.getFloatPixelsRef());
		threshold = 0; // We go from the higher distance to the kinect (lower position) to the lower distance
	}
	else if (GetROICalibState() == ROI_CALIBRATION_STATE_MOVE_UP)
	{
		ofLogVerbose("KinectProjector") << "updateROIFromDepthImage(): ROI_CALIBRATION_STATE_MOVE_UP";
		while (threshold < 255)
		{
			cvThreshold(thresholdedImage.getCvImage(), thresholdedImage.getCvImage(), 255 - threshold, 255, CV_THRESH_TOZERO_INV);
			thresholdedImage.updateTexture();
			//			SaveDepthDebugImageNative(thresholdedImage, counter++);
			contourFinder.findContours(thresholdedImage, 12, kinectRes.x * kinectRes.y, 5, true, false);
			ofPolyline small = ofPolyline();
			for (int i = 0; i < contourFinder.nBlobs; i++)
			{
				ofxCvBlob blobContour = contourFinder.blobs[i];
				if (blobContour.hole)
				{
					ofPolyline poly = ofPolyline(blobContour.pts);
					if (poly.inside(kinectRes.x / 2, kinectRes.y / 2))
					{
						if (small.size() == 0 || poly.getArea() < small.getArea())
						{
							small = poly;
						}
					}
				}
			}
			if (large.getArea() < small.getArea())
			{
				ofLogVerbose("KinectProjector") << "updateROIFromDepthImage(): updating ROI";
				large = small;
			}
			threshold += 1;
		}
		if (large.getArea() == 0)
		{
			ofLogVerbose("KinectProjector") << "Calibration failed: The sandbox walls could not be found";
			calibModal->hide();
			confirmModal->setTitle("Calibration failed");
			setConfirmModalMessage("SANDBOX_WALLS_NOT_FOUND");
			// confirmModal->setMessage("The sandbox walls could not be found.");
			setConfirmModalState(CONFIRM_MODAL_OPENED);
			//            calibrating = false;
			setApplicationState(APPLICATION_STATE_SETUP);
			updateStatusGUI();
		}
		else
		{
			kinectROI = large.getBoundingBox();
			//            insideROIPoly = large.getResampledBySpacing(10);
			kinectROI.standardize();
			calibModal->setMessage("Sand area successfully detected");
			ofLogVerbose("KinectProjector") << "updateROIFromDepthImage(): final kinectROI : " << kinectROI;
			setNewKinectROI();
			if (GetCalibrationState() == CALIBRATION_STATE_ROI_AUTO_DETERMINATION)
			{
				setApplicationState(APPLICATION_STATE_SETUP);
				//                calibrating = false;
				calibModal->hide();
				updateStatusGUI();
			}
		}
		setROICalibState(ROI_CALIBRATION_STATE_DONE);
	}
	else if (GetROICalibState() == ROI_CALIBRATION_STATE_DONE)
	{
	}
}

void KinectProjector::updateROIFromFile()
{
	string settingsFile = "settings/kinectProjectorSettings.xml";

	ofXml xml;
	if (xml.load(settingsFile))
	{
		xml.setTo("KINECTSETTINGS");
		kinectROI = xml.getValue<ofRectangle>("kinectROI");
		setNewKinectROI();
		setROICalibState(ROI_CALIBRATION_STATE_DONE);
		return;
	}
	ofLogVerbose("KinectProjector") << "updateROIFromFile(): could not read settings/kinectProjectorSettings.xml";
	setApplicationState(APPLICATION_STATE_SETUP);
	updateStatusGUI();
}

void KinectProjector::setMaxKinectGrabberROI()
{
	updateKinectGrabberROI(ofRectangle(0, 0, kinectRes.x, kinectRes.y));
}

void KinectProjector::setNewKinectROI()
{
	setNewKinectROI(true);
}

void KinectProjector::setNewKinectROI(bool updateGui)
{
	CheckAndNormalizeKinectROI();

	// Cast to integer values
	kinectROI.x = static_cast<int>(kinectROI.x);
	kinectROI.y = static_cast<int>(kinectROI.y);
	kinectROI.width = static_cast<int>(kinectROI.width);
	kinectROI.height = static_cast<int>(kinectROI.height);

	ofLogVerbose("KinectProjector") << "setNewKinectROI : " << kinectROI;

	// Update states variables
	ROIcalibrated = true;
	//    ROIUpdated = true;
	saveCalibrationAndSettings();
	updateKinectGrabberROI(kinectROI);
	if (updateGui)
	{
		updateStatusGUI();
	}
	updateStateEvent();
}

void KinectProjector::updateKinectGrabberROI(ofRectangle ROI)
{
	kinectgrabber.performInThread([ROI](KinectGrabber &kg) {
		kg.setKinectROI(ROI);
	});
	//    while (kinectgrabber.isImageStabilized()){
	//    } // Wait for kinectgrabber to reset buffers
	imageStabilized = false; // Now we can wait for a clean new depth frame
}

std::string KinectProjector::GetTimeAndDateString()
{
	time_t t = time(0); // get time now
	struct tm *now = localtime(&t);
	std::stringstream ss;

	ss << now->tm_mday << '-'
	   << (now->tm_mon + 1) << '-'
	   << (now->tm_year + 1900) << '-'
	   << now->tm_hour << '-'
	   << now->tm_min << '-'
	   << now->tm_sec;

	return ss.str();
}

bool KinectProjector::savePointPair()
{
	std::string ppK = ofToDataPath(DebugFileOutDir + "CalibrationPointPairsKinect.txt");
	std::string ppP = ofToDataPath(DebugFileOutDir + "CalibrationPointPairsKinect.txt");
	std::ofstream ppKo(ppK);
	std::ofstream ppPo(ppP);

	for (int i = 0; i < pairsKinect.size(); i++)
	{
		ppKo << pairsKinect[i].x << " " << pairsKinect[i].y << " " << pairsKinect[i].z << " " << i << std::endl;
	}

	for (int i = 0; i < pairsProjector.size(); i++)
	{
		ppPo << pairsProjector[i].x << " " << pairsProjector[i].y << " " << i << std::endl;
	}
	return true;
}

void KinectProjector::updateProjKinectAutoCalibration()
{
	if (autoCalibState == AUTOCALIB_STATE_INIT_FIRST_PLANE)
	{
		kinectgrabber.performInThread([](KinectGrabber &kg) {
			kg.setMaxOffset(0);
		});
		calibrationText = "Stabilizing acquisition";
		setAutoCalibrationState(AUTOCALIB_STATE_INIT_POINT);
		updateStatusGUI();
	}
	else if (autoCalibState == AUTOCALIB_STATE_INIT_POINT && imageStabilized)
	{
		calibrationText = "Acquiring sea level plane";
		updateStatusGUI();
		updateBasePlane(); // Find base plane
		if (!basePlaneComputed)
		{
			setApplicationState(APPLICATION_STATE_SETUP);
			calibrationText = "Failed to acquire sea level plane";
			updateStatusGUI();
			return;
		}
		calibrationText = "Sea level plane estimated";
		updateStatusGUI();

		autoCalibPts = new ofPoint[10];
		float cs = 4 * chessboardSize / 3;
		float css = 3 * chessboardSize / 4;
		ofPoint sc = ofPoint(projRes.x / 2, projRes.y / 2);

		// Prepare 10 locations for the calibration chessboard
		// With a point of (0,0) the chessboard will be placed with the center in  the center of the projector
		// a point of -sc will the chessboard will be placed with the center in the upper left corner
		// Rasmus modified sequence with a center chessboard first to check if everything is working
		autoCalibPts[0] = ofPoint(0, 0);								  // Center
		autoCalibPts[1] = ofPoint(projRes.x - cs, cs) - sc;				  // upper right
		autoCalibPts[2] = ofPoint(projRes.x - cs, projRes.y - cs) - sc;   // Lower right
		autoCalibPts[3] = ofPoint(cs, projRes.y - cs) - sc;				  // Lower left
		autoCalibPts[4] = ofPoint(cs, cs) - sc;							  // upper left
		autoCalibPts[5] = ofPoint(0, 0);								  // Center
		autoCalibPts[6] = ofPoint(projRes.x - css, css) - sc;			  // upper right
		autoCalibPts[7] = ofPoint(projRes.x - css, projRes.y - css) - sc; // Lower right
		autoCalibPts[8] = ofPoint(css, projRes.y - css) - sc;			  // Lower left
		autoCalibPts[9] = ofPoint(css, css) - sc;						  // upper left

		currentCalibPts = 0;
		upframe = false;
		trials = 0;
		TemporalFrameCounter = 0;

		ofPoint dispPt = ofPoint(projRes.x / 2, projRes.y / 2) + autoCalibPts[currentCalibPts]; //
		drawChessboard(dispPt.x, dispPt.y, chessboardSize);										// We can now draw the next chess board

		setAutoCalibrationState(AUTOCALIB_STATE_NEXT_POINT);
	}
	else if (autoCalibState == AUTOCALIB_STATE_NEXT_POINT && imageStabilized)
	{
		if (!(TemporalFrameCounter % 20))
			ofLogVerbose("KinectProjector") << "autoCalib(): Got frame " + ofToString(TemporalFrameCounter) + " / " + ofToString(TemporalFrameFilter.getBufferSize() + 3) + " for temporal filter";

		// We want to have a buffer of images that are only focusing on one chess pattern
		if (TemporalFrameCounter++ > TemporalFrameFilter.getBufferSize() + 3)
		{
			CalibrateNextPoint();
			TemporalFrameCounter = 0;
		}
	}
	else if (autoCalibState == AUTOCALIB_STATE_COMPUTE)
	{
		updateKinectGrabberROI(kinectROI); // Goes back to kinectROI and maxoffset
		kinectgrabber.performInThread([this](KinectGrabber &kg) {
			kg.setMaxOffset(this->maxOffset);
		});
		if (pairsKinect.size() == 0)
		{
			ofLogVerbose("KinectProjector") << "autoCalib(): Error: No points acquired !!";
			calibrationText = "Calibration failed: No points acquired";
			setApplicationState(APPLICATION_STATE_SETUP);
			updateStatusGUI();
		}
		else
		{
			ofLogVerbose("KinectProjector") << "autoCalib(): Calibrating";
			kpt->calibrate(pairsKinect, pairsProjector);
			kinectProjMatrix = kpt->getProjectionMatrix();

			double ReprojectionError = ComputeReprojectionError(DumpDebugFiles);
			ofLogVerbose("KinectProjector") << "autoCalib(): ReprojectionError " + ofToString(ReprojectionError);

			if (ReprojectionError > 50)
			{
				ofLogVerbose("KinectProjector") << "autoCalib(): ReprojectionError too big. Something wrong with projection matrix";
				setProjKinectCalibrated(false);
				projKinectCalibrationUpdated = false;
				setApplicationState(APPLICATION_STATE_SETUP);
				calibrationText = "Calibration failed - reprojection error too big";
				updateErrorEvent("CALIBRATION_FAILED_ERROR_TOO_BIG");
				updateStatusGUI();
				return;
			}

			// Rasmus update - I am not sure it is good to override the manual ROI
			// updateROIFromCalibration(); // Compute the limite of the ROI according to the projected area
			setProjKinectCalibrated(true);
			projKinectCalibrationUpdated = true;
			setApplicationState(APPLICATION_STATE_SETUP);
			calibrationText = "Calibration successful";
			
			//saveCalibrationAndSettings(); // Already done in updateROIFromCalibration
			if (kpt->saveCalibration("settings/calibration.xml"))
			{
				ofLogVerbose("KinectProjector") << "update(): initialisation: Calibration saved ";
			}
			else
			{
				ofLogVerbose("KinectProjector") << "update(): initialisation: Calibration could not be saved ";
			}
			updateStatusGUI();
		}
		setAutoCalibrationState(AUTOCALIB_STATE_DONE);
	}
	else if (!imageStabilized)
	{
		ofLogVerbose("KinectProjector") << "updateProjKinectAutoCalibration(): image not stabilised";
	}
	else if (autoCalibState == AUTOCALIB_STATE_DONE)
	{
	}
}

// Compute the error when using the projection matrix to project calibration Kinect points into Project space
// and comparing with calibration projector points
double KinectProjector::ComputeReprojectionError(bool WriteFile)
{
	std::string oErrors = ofToDataPath(DebugFileOutDir + "CalibrationReprojectionErrors_" + GetTimeAndDateString() + ".txt");

	double PError = 0;

	for (int i = 0; i < pairsKinect.size(); i++)
	{
		ofVec4f wc = pairsKinect[i];
		wc.w = 1;

		ofVec4f screenPos = kinectProjMatrix * wc;
		ofVec2f projectedPoint(screenPos.x / screenPos.z, screenPos.y / screenPos.z);
		ofVec2f projP = pairsProjector[i];

		double D = sqrt((projectedPoint.x - projP.x) * (projectedPoint.x - projP.x) + (projectedPoint.y - projP.y) * (projectedPoint.y - projP.y));

		PError += D;
	}
	PError /= (double)pairsKinect.size();

	if (WriteFile)
	{
		std::ofstream fost2(oErrors.c_str());

		for (int i = 0; i < pairsKinect.size(); i++)
		{
			ofVec4f wc = pairsKinect[i];
			wc.w = 1;

			ofVec4f screenPos = kinectProjMatrix * wc;
			ofVec2f projectedPoint(screenPos.x / screenPos.z, screenPos.y / screenPos.z);
			ofVec2f projP = pairsProjector[i];

			double D = sqrt((projectedPoint.x - projP.x) * (projectedPoint.x - projP.x) + (projectedPoint.y - projP.y) * (projectedPoint.y - projP.y));

			fost2 << wc.x << ", " << wc.y << ", " << wc.z << ", "
				  << projP.x << ", " << projP.y << ", " << projectedPoint.x << ", " << projectedPoint.y << ", " << D << std::endl;
		}
	}

	return PError;
}

void KinectProjector::CalibrateNextPoint()
{
	if (currentCalibPts < 5 || (upframe && currentCalibPts < 10))
	{
		if (!upframe)
		{
			calibrationText = "Calibration (low) # " + std::to_string(currentCalibPts + 1) + "/5";
			updateStatusGUI();
		}
		else
		{
			calibrationText = "Calibration (high) #  " + std::to_string(currentCalibPts - 4) + "/5";
			updateStatusGUI();
		}

		// Current RGB frame - probably with rolling shutter problems
		cvRgbImage = ofxCv::toCv(kinectColorImage.getPixels());

		ofxCvGrayscaleImage tempImage;
		if (TemporalFilteringType == 0)
			tempImage.setFromPixels(TemporalFrameFilter.getMedianFilteredImage(), kinectColorImage.width, kinectColorImage.height);
		if (TemporalFilteringType == 1)
			tempImage.setFromPixels(TemporalFrameFilter.getAverageFilteredColImage(), kinectColorImage.width, kinectColorImage.height);

		ProcessChessBoardInput(tempImage);

		if (DumpDebugFiles)
		{
			std::string tname = DebugFileOutDir + "ChessboardImage_" + GetTimeAndDateString() + "_" + ofToString(currentCalibPts) + "_try_" + ofToString(trials) + ".png";
			ofSaveImage(tempImage.getPixels(), tname);
		}

		cvGrayImage = ofxCv::toCv(tempImage.getPixels());

		cv::Rect tempROI((int)kinectROI.x, (int)kinectROI.y, (int)kinectROI.width, (int)kinectROI.height);
		cv::Mat cvGrayROI = cvGrayImage(tempROI);

		cv::Size patternSize = cv::Size(chessboardX - 1, chessboardY - 1);
		//	int chessFlags = cv::CALIB_CB_ADAPTIVE_THRESH + cv::CALIB_CB_FAST_CHECK;
		int chessFlags = 0;

		//bool foundChessboard = findChessboardCorners(cvGrayImage, patternSize, cvPoints, chessFlags);
		bool foundChessboard = findChessboardCorners(cvGrayROI, patternSize, cvPoints, chessFlags);

		if (!foundChessboard)
		{
			int chessFlags = cv::CALIB_CB_ADAPTIVE_THRESH + cv::CALIB_CB_FAST_CHECK;
			foundChessboard = findChessboardCorners(cvGrayROI, patternSize, cvPoints, chessFlags);
		}

		// Changed logic so the "cleared" flag is not used - we do a long frame average instead
		if (foundChessboard)
		{
			for (int i = 0; i < cvPoints.size(); i++)
			{
				cvPoints[i].x += tempROI.x;
				cvPoints[i].y += tempROI.y;
			}

			cornerSubPix(cvGrayImage, cvPoints, cv::Size(2, 2), cv::Size(-1, -1), // Rasmus: changed search size to 2 from 11 - since this caused false findings
						 cv::TermCriteria(CV_TERMCRIT_EPS + CV_TERMCRIT_ITER, 30, 0.1));

			drawChessboardCorners(cvRgbImage, patternSize, cv::Mat(cvPoints), foundChessboard);

			if (DumpDebugFiles)
			{
				std::string tname = DebugFileOutDir + "FoundChessboard_" + GetTimeAndDateString() + "_" + ofToString(currentCalibPts) + "_try_" + ofToString(trials) + ".png";
				ofSaveImage(kinectColorImage.getPixels(), tname);
			}

			kinectColorImage.updateTexture();
			fboMainWindow.begin();
			kinectColorImage.draw(0, 0);
			fboMainWindow.end();

			ofLogVerbose("KinectProjector") << "autoCalib(): Chessboard found for point :" << currentCalibPts;
			bool okchess = addPointPair();

			if (okchess)
			{
				trials = 0;
				currentCalibPts++;
				ofPoint dispPt = ofPoint(projRes.x / 2, projRes.y / 2) + autoCalibPts[currentCalibPts]; // Compute next chessboard position
				drawChessboard(dispPt.x, dispPt.y, chessboardSize);										// We can now draw the next chess board
			}
			else
			{
				// We cannot get all depth points for the chessboard
				trials++;
				ofLogVerbose("KinectProjector") << "autoCalib(): Depth points of chessboard not allfound on trial : " << trials;
				if (trials > 3)
				{
					// Move the chessboard closer to the center of the screen
					ofLogVerbose("KinectProjector") << "autoCalib(): Chessboard could not be found moving chessboard closer to center ";
					autoCalibPts[currentCalibPts] = 4 * autoCalibPts[currentCalibPts] / 5;
					ofPoint dispPt = ofPoint(projRes.x / 2, projRes.y / 2) + autoCalibPts[currentCalibPts]; // Compute next chessboard position
					drawChessboard(dispPt.x, dispPt.y, chessboardSize);										// We can now draw the next chess board
					trials = 0;
				}
			}
		}
		else
		{
			// We cannot find the chessboard
			trials++;
			ofLogVerbose("KinectProjector") << "autoCalib(): Chessboard not found on trial : " << trials;
			if (trials > 3)
			{
				// Move the chessboard closer to the center of the screen
				ofLogVerbose("KinectProjector") << "autoCalib(): Chessboard could not be found moving chessboard closer to center ";
				autoCalibPts[currentCalibPts] = 3 * autoCalibPts[currentCalibPts] / 4;

				ofPoint dispPt = ofPoint(projRes.x / 2, projRes.y / 2) + autoCalibPts[currentCalibPts]; // Compute next chessboard position
				drawChessboard(dispPt.x, dispPt.y, chessboardSize);										// We can now draw the next chess board
				trials = 0;
			}
		}
	}
	else
	{
		if (upframe)
		{ // We are done
			calibrationText = "Updating acquisition ceiling";
			updateMaxOffset(); // Find max offset
			setAutoCalibrationState(AUTOCALIB_STATE_COMPUTE);
			updateStatusGUI();
		}
		else
		{ // We ask for higher points
			calibModal->hide();
			setConfirmModalState(CONFIRM_MODAL_OPENED);
			setConfirmModalMessage("COVER_SANDBOX_WITH_BOARD");
			// confirmModal->setMessage("Please cover the sandbox with a board and press ok.");
		}
	}
}

//TODO: Add manual Prj Kinect calibration
void KinectProjector::updateProjKinectManualCalibration()
{
	// Draw a Chessboard
	drawChessboard(ofGetMouseX(), ofGetMouseY(), chessboardSize);
	// Try to find the chess board on the kinect color image
	cvRgbImage = ofxCv::toCv(kinectColorImage.getPixels());
	cv::Size patternSize = cv::Size(chessboardX - 1, chessboardY - 1);
	int chessFlags = cv::CALIB_CB_ADAPTIVE_THRESH + cv::CALIB_CB_FAST_CHECK;
	bool foundChessboard = findChessboardCorners(cvRgbImage, patternSize, cvPoints, chessFlags);
	if (foundChessboard)
	{
		cv::Mat gray;
		cvtColor(cvRgbImage, gray, CV_RGB2GRAY);
		cornerSubPix(gray, cvPoints, cv::Size(11, 11), cv::Size(-1, -1),
					 cv::TermCriteria(CV_TERMCRIT_EPS + CV_TERMCRIT_ITER, 30, 0.1));
		drawChessboardCorners(cvRgbImage, patternSize, cv::Mat(cvPoints), foundChessboard);
	}
}

void KinectProjector::updateBasePlane()
{
	basePlaneComputed = false;
	updateStatusGUI();

	ofRectangle smallROI = kinectROI;
	smallROI.scaleFromCenter(0.75); // Reduce ROI to avoid problems with borders
	ofLogVerbose("KinectProjector") << "updateBasePlane(): smallROI: " << smallROI;
	int sw = static_cast<int>(smallROI.width);
	int sh = static_cast<int>(smallROI.height);
	int sl = static_cast<int>(smallROI.getLeft());
	int st = static_cast<int>(smallROI.getTop());
	ofLogVerbose("KinectProjector") << "updateBasePlane(): sw: " << sw << " sh : " << sh << " sl : " << sl << " st : " << st << " sw*sh : " << sw * sh;
	if (sw * sh == 0)
	{
		ofLogVerbose("KinectProjector") << "updateBasePlane(): smallROI is null, cannot compute base plane normal";
		return;
	}
	ofVec4f pt;
	ofVec3f *points;
	points = new ofVec3f[sw * sh];
	ofLogVerbose("KinectProjector") << "updateBasePlane(): Computing points in smallROI : " << sw * sh;
	for (int x = 0; x < sw; x++)
	{
		for (int y = 0; y < sh; y++)
		{
			points[x + y * sw] = kinectCoordToWorldCoord(x + sl, y + st);
		}
	}
	ofLogVerbose("KinectProjector") << "updateBasePlane(): Computing plane from points";
	basePlaneEq = plane_from_points(points, sw * sh);
	if (basePlaneEq.x == 0 && basePlaneEq.y == 0 && basePlaneEq.z == 0)
	{
		ofLogVerbose("KinectProjector") << "updateBasePlane(): plane_from_points could not compute basePlane";
		return;
	}

	basePlaneNormal = ofVec3f(basePlaneEq);
	basePlaneOffset = ofVec3f(0, 0, -basePlaneEq.w);
	basePlaneNormalBack = basePlaneNormal;
	basePlaneOffsetBack = basePlaneOffset;
	basePlaneUpdated = true;
	basePlaneComputed = true;
	updateStatusGUI();
}

void KinectProjector::updateMaxOffset()
{
	ofRectangle smallROI = kinectROI;
	smallROI.scaleFromCenter(0.75); // Reduce ROI to avoid problems with borders
	ofLogVerbose("KinectProjector") << "updateMaxOffset(): smallROI: " << smallROI;
	int sw = static_cast<int>(smallROI.width);
	int sh = static_cast<int>(smallROI.height);
	int sl = static_cast<int>(smallROI.getLeft());
	int st = static_cast<int>(smallROI.getTop());
	ofLogVerbose("KinectProjector") << "updateMaxOffset(): sw: " << sw << " sh : " << sh << " sl : " << sl << " st : " << st << " sw*sh : " << sw * sh;
	if (sw * sh == 0)
	{
		ofLogVerbose("KinectProjector") << "updateMaxOffset(): smallROI is null, cannot compute base plane normal";
		return;
	}
	ofVec4f pt;
	ofVec3f *points;
	points = new ofVec3f[sw * sh];
	ofLogVerbose("KinectProjector") << "updateMaxOffset(): Computing points in smallROI : " << sw * sh;
	for (int x = 0; x < sw; x++)
	{
		for (int y = 0; y < sh; y++)
		{
			points[x + y * sw] = kinectCoordToWorldCoord(x + sl, y + st); //vertexCcvertexCc;
		}
	}
	ofLogVerbose("KinectProjector") << "updateMaxOffset(): Computing plane from points";
	ofVec4f eqoff = plane_from_points(points, sw * sh);
	maxOffset = -eqoff.w - maxOffsetSafeRange;
	maxOffsetBack = maxOffset;
	// Update max Offset
	ofLogVerbose("KinectProjector") << "updateMaxOffset(): maxOffset" << maxOffset;
	kinectgrabber.performInThread([this](KinectGrabber &kg) {
		kg.setMaxOffset(this->maxOffset);
	});
}

bool KinectProjector::addPointPair()
{
	bool okchess = true;
	string resultMessage;
	ofLogVerbose("KinectProjector") << "addPointPair(): Adding point pair in kinect world coordinates";
	int nDepthPoints = 0;
	for (int i = 0; i < cvPoints.size(); i++)
	{
		ofVec3f worldPoint = kinectCoordToWorldCoord(cvPoints[i].x, cvPoints[i].y);
		if (worldPoint.z > 0)
			nDepthPoints++;
	}
	if (nDepthPoints == (chessboardX - 1) * (chessboardY - 1))
	{
		for (int i = 0; i < cvPoints.size(); i++)
		{
			ofVec3f worldPoint = kinectCoordToWorldCoord(cvPoints[i].x, cvPoints[i].y);
			pairsKinect.push_back(worldPoint);
			pairsProjector.push_back(currentProjectorPoints[i]);
		}
		resultMessage = "addPointPair(): Added " + ofToString((chessboardX - 1) * (chessboardY - 1)) + " points pairs.";
		if (DumpDebugFiles)
		{
			savePointPair();
		}
	}
	else
	{
		resultMessage = "addPointPair(): Points not added because not all chessboard\npoints' depth known. Try re-positionining.";
		okchess = false;
	}
	ofLogVerbose("KinectProjector") << resultMessage;
	return okchess;
}

void KinectProjector::askToFlattenSand()
{
	fboProjWindow.begin();
	ofBackground(255);
	fboProjWindow.end();
	setConfirmModalMessage("FLATTEN_SAND");
	// confirmModal->setMessage("Please flatten the sand surface.");
	setConfirmModalState(CONFIRM_MODAL_OPENED);
	waitingForFlattenSand = true;
}

void KinectProjector::drawProjectorWindow()
{
	fboProjWindow.draw(0, 0);
}

void KinectProjector::drawMainWindow(float x, float y, float width, float height)
{

	bool forceScale = false;
	if (forceScale)
	{
		fboMainWindow.draw(x, y, width, height);
	}
	else
	{
		fboMainWindow.draw(x, y);
	}

	if (displayGui)
	{
		gui->draw();
		StatusGUI->draw();
	}
}

void KinectProjector::drawChessboard(int x, int y, int chessboardSize)
{
	fboProjWindow.begin();
	ofFill();
	// Draw the calibration chess board on the projector window
	float w = chessboardSize / chessboardX;
	float h = chessboardSize / chessboardY;

	float xf = x - chessboardSize / 2; // x and y are chess board center size
	float yf = y - chessboardSize / 2;

	currentProjectorPoints.clear();

	ofClear(255, 255, 255, 0);
	ofBackground(255);
	ofSetColor(0);
	ofTranslate(xf, yf);
	for (int j = 0; j < chessboardY; j++)
	{
		for (int i = 0; i < chessboardX; i++)
		{
			int x0 = ofMap(i, 0, chessboardX, 0, chessboardSize);
			int y0 = ofMap(j, 0, chessboardY, 0, chessboardSize);
			if (j > 0 && i > 0)
			{
				currentProjectorPoints.push_back(ofVec2f(xf + x0, yf + y0));
			}
			if ((i + j) % 2 == 0)
				ofDrawRectangle(x0, y0, w, h);
		}
	}
	ofSetColor(255);
	fboProjWindow.end();
}

void KinectProjector::drawGradField()
{
	ofClear(255, 0);
	for (int rowPos = 0; rowPos < gradFieldrows; rowPos++)
	{
		for (int colPos = 0; colPos < gradFieldcols; colPos++)
		{
			float x = colPos * gradFieldResolution + gradFieldResolution / 2;
			float y = rowPos * gradFieldResolution + gradFieldResolution / 2;
			ofVec2f projectedPoint = kinectCoordToProjCoord(x, y);
			int ind = colPos + rowPos * gradFieldcols;
			ofVec2f v2 = gradField[ind];
			v2 *= arrowLength;

			ofSetColor(255, 0, 0, 255);
			if (ind == fishInd)
				ofSetColor(0, 255, 0, 255);

			drawArrow(projectedPoint, v2);
		}
	}
}

void KinectProjector::drawArrow(ofVec2f projectedPoint, ofVec2f v1)
{
	float angle = ofRadToDeg(atan2(v1.y, v1.x));
	float length = v1.length();
	ofFill();
	ofPushMatrix();
	ofTranslate(projectedPoint);
	ofRotate(angle);
	ofSetColor(255, 0, 0, 255);
	ofDrawLine(0, 0, length, 0);
	ofDrawLine(length, 0, length - 7, 5);
	ofDrawLine(length, 0, length - 7, -5);
	ofPopMatrix();
}

void KinectProjector::updateNativeScale(float scaleMin, float scaleMax)
{
	FilteredDepthImage.setNativeScale(scaleMin, scaleMax);
}

ofVec2f KinectProjector::kinectCoordToProjCoord(float x, float y) // x, y in kinect pixel coord
{
	return worldCoordToProjCoord(kinectCoordToWorldCoord(x, y));
}

ofVec2f KinectProjector::kinectCoordToProjCoord(float x, float y, float z)
{
	ofVec4f kc = ofVec2f(x, y);
	kc.z = z;
	kc.w = 1;
	ofVec4f wc = kinectWorldMatrix * kc * kc.z;

	return worldCoordToProjCoord(wc);
}

ofVec2f KinectProjector::worldCoordToProjCoord(ofVec3f vin)
{
	ofVec4f wc = vin;
	wc.w = 1;
	ofVec4f screenPos = kinectProjMatrix * wc;
	ofVec2f projectedPoint(screenPos.x / screenPos.z, screenPos.y / screenPos.z);
	return projectedPoint;
}

ofVec3f KinectProjector::projCoordAndWorldZToWorldCoord(float projX, float projY, float worldZ)
{
	float a = kinectProjMatrix(0, 0) - kinectProjMatrix(2, 0) * projX;
	float b = kinectProjMatrix(0, 1) - kinectProjMatrix(2, 1) * projX;
	float c = (kinectProjMatrix(2, 2) * worldZ + 1) * projX - (kinectProjMatrix(0, 2) * worldZ + kinectProjMatrix(0, 3));
	float d = kinectProjMatrix(1, 0) - kinectProjMatrix(2, 0) * projY;
	float e = kinectProjMatrix(1, 1) - kinectProjMatrix(2, 1) * projY;
	float f = (kinectProjMatrix(2, 2) * worldZ + 1) * projY - (kinectProjMatrix(1, 2) * worldZ + kinectProjMatrix(1, 3));

	float det = a * e - b * d;
	if (det == 0)
		return ofVec3f(0);
	float y = (a * f - d * c) / det;
	float x = (c * e - b * f) / det;
	return ofVec3f(x, y, worldZ);
}

ofVec3f KinectProjector::kinectCoordToWorldCoord(float x, float y) // x, y in kinect pixel coord
{
	// Simple crash avoidence
	if (y < 0)
		y = 0;
	if (y >= kinectRes.y)
		y = kinectRes.y - 1;
	if (x < 0)
		x = 0;
	if (x >= kinectRes.x)
		x = kinectRes.x - 1;

	ofVec4f kc = ofVec2f(x, y);
	int ind = static_cast<int>(y) * kinectRes.x + static_cast<int>(x);
	kc.z = FilteredDepthImage.getFloatPixelsRef().getData()[ind];
	//if (kc.z == 0)
	//	ofLogVerbose("KinectProjector") << "kinectCoordToWorldCoord z coordinate 0";
	//if (kc.z == 4000)
	//	ofLogVerbose("KinectProjector") << "kinectCoordToWorldCoord z coordinate 4000 (invalid)";

	kc.w = 1;
	ofVec4f wc = kinectWorldMatrix * kc * kc.z;
	return ofVec3f(wc);
}

ofVec2f KinectProjector::worldCoordTokinectCoord(ofVec3f wc)
{
	float x = (wc.x / wc.z - kinectWorldMatrix(0, 3)) / kinectWorldMatrix(0, 0);
	float y = (wc.y / wc.z - kinectWorldMatrix(1, 3)) / kinectWorldMatrix(1, 1);
	return ofVec2f(x, y);
}

ofVec3f KinectProjector::RawKinectCoordToWorldCoord(float x, float y) // x, y in kinect pixel coord
{
	ofVec4f kc = ofVec3f(x, y, kinectgrabber.getRawDepthAt(static_cast<int>(x), static_cast<int>(y)));
	kc.w = 1;
	ofVec4f wc = kinectWorldMatrix * kc * kc.z;
	return ofVec3f(wc);
}

float KinectProjector::elevationAtKinectCoord(float x, float y) // x, y in kinect pixel coordinate
{
	ofVec4f wc = kinectCoordToWorldCoord(x, y);
	wc.w = 1;
	float elevation = -basePlaneEq.dot(wc);
	return elevation;
}

float KinectProjector::elevationToKinectDepth(float elevation, float x, float y) // x, y in kinect pixel coordinate
{
	ofVec4f wc = kinectCoordToWorldCoord(x, y);
	wc.z = 0;
	wc.w = 1;
	float kinectDepth = -(basePlaneEq.dot(wc) + elevation) / basePlaneEq.z;
	return kinectDepth;
}

ofVec2f KinectProjector::gradientAtKinectCoord(float x, float y)
{
	int ind = static_cast<int>(floor(x / gradFieldResolution)) + gradFieldcols * static_cast<int>(floor(y / gradFieldResolution));
	fishInd = ind;
	return gradField[ind];
}

void KinectProjector::setupGui()
{
	// instantiate and position the gui //
	gui = new ofxDatGui(ofxDatGuiAnchor::TOP_RIGHT);
	gui->addButton("RUN!")->setName("Start Application");
	gui->addBreak();
	gui->addFRM();
	fpsKinectText = gui->addTextInput("Kinect FPS", "0");
	gui->addBreak();

	auto advancedFolder = gui->addFolder("Advanced", ofColor::purple);
	advancedFolder->addToggle("Display kinect depth view", drawKinectView)->setName(CMP_DRAW_KINECT_DEPTH_VIEW);
	advancedFolder->addToggle("Display kinect color view", drawKinectColorView)->setName(CMP_DRAW_KINECT_COLOR_VIEW);
	advancedFolder->addToggle(CMP_DUMP_DEBUG, DumpDebugFiles);
	advancedFolder->addSlider(CMP_CEILING, -300, 300, 0);
	advancedFolder->addToggle(CMP_SPATIAL_FILTERING, spatialFiltering);
	advancedFolder->addToggle(CMP_INPAINT_OUTLIERS, doInpainting);
	advancedFolder->addToggle(CMP_FULL_FRAME_FILTERING, doFullFrameFiltering);
	advancedFolder->addToggle(CMP_QUICK_REACTION, followBigChanges);
	advancedFolder->addSlider(CMP_AVERAGING, 1, 40, numAveragingSlots)->setPrecision(0);
	advancedFolder->addSlider(CMP_TILT_X, -30, 30, tiltX);
	advancedFolder->addSlider(CMP_TILT_Y, -30, 30, tiltY);
	advancedFolder->addSlider(CMP_VERTICAL_OFFSET, -100, 100, verticalOffset);
	advancedFolder->addButton("Reset sea level");
	advancedFolder->addBreak();

	auto calibrationFolder = gui->addFolder("Calibration", ofColor::darkCyan);
	calibrationFolder->addButton("Manually define sand region");
	calibrationFolder->addButton("Automatically calibrate kinect & projector");
	calibrationFolder->addButton("Auto Adjust ROI");
	calibrationFolder->addToggle(CMP_SHOW_ROI_ON_SAND, doShowROIonProjector);

	//	  advancedFolder->addButton("Draw ROI")->setName("Draw ROI");
	//    advancedFolder->addButton("Calibrate")->setName("Full Calibration");
	//	  advancedFolder->addButton("Update ROI from calibration");
	//    gui->addButton("Automatically detect sand region");
	//    calibrationFolder->addButton("Manually define sand region");
	//    gui->addButton("Automatically calibrate kinect & projector");
	//    calibrationFolder->addButton("Manually calibrate kinect & projector");
	//    gui->addBreak();

	gui->addHeader(":: Settings ::", false);

	// once the gui has been assembled, register callbacks to listen for component specific events //
	gui->onButtonEvent(this, &KinectProjector::onButtonEvent);
	gui->onToggleEvent(this, &KinectProjector::onToggleEvent);
	gui->onSliderEvent(this, &KinectProjector::onSliderEvent);

	// disactivate autodraw
	gui->setAutoDraw(false);

	StatusGUI = new ofxDatGui(ofxDatGuiAnchor::BOTTOM_LEFT);
	StatusGUI->addLabel("Application Status");
	StatusGUI->addLabel("Kinect Status");
	StatusGUI->addLabel("ROI Status");
	StatusGUI->addLabel("Baseplane Status");
	StatusGUI->addLabel("Calibration Status");
	StatusGUI->addLabel("Calibration Step");
	StatusGUI->addLabel("Projector Status");
	StatusGUI->addHeader(":: Status ::", false);
	StatusGUI->setAutoDraw(false);
}

string KinectProjector::startApplication()
{
	return startApplication(true);
};


string KinectProjector::checkStartReady(bool updateFlag = true) {
	
	if (!kinectOpened)
	{
		ofLogVerbose("KinectProjector") << "KinectProjector.startApplication(): Kinect is not running ";
		updateErrorEvent("KINECT_NOT_RUNNING");
		return "KINECT_NOT_RUNNING";
	}

	if (!isCalibrated())
	{
		ofLogVerbose("KinectProjector") << "KinectProjector.startApplication(): Kinect projector not calibrated - trying to load calibration.xml";
		//Try to load calibration file if possible
		if (kpt->loadCalibration("settings/calibration.xml"))
		{
			ofLogVerbose("KinectProjector") << "KinectProjector.setup(): Calibration loaded ";
			kinectProjMatrix = kpt->getProjectionMatrix();
			ofLogVerbose("KinectProjector") << "KinectProjector.setup(): kinectProjMatrix: " << kinectProjMatrix;
			setProjKinectCalibrated(true);
			projKinectCalibrationUpdated = true;
			updateStateEvent();
			updateFlag ? updateStatusGUI() : noop;
		}
		else
		{
			ofLogVerbose("KinectProjector") << "KinectProjector.startApplication(): Calibration could not be loaded";
			updateErrorEvent("NO_CALIBRATION");
			return "NO_CALIBRATION";
		}
	}

	if (!ROIcalibrated)
	{
		ofLogVerbose("KinectProjector") << "KinectProjector.startApplication(): Kinect ROI not calibrated - trying to load kinectProjectorSettings.xml";
		//Try to load settings file if possible
		if (loadSettings())
		{
			ofLogVerbose("KinectProjector") << "KinectProjector.setup(): Settings loaded ";
			setNewKinectROI();
			ROIcalibrated = true;
			basePlaneComputed = true;
			setFullFrameFiltering(doFullFrameFiltering, updateFlag);
			setInPainting(doInpainting, updateFlag);
			setFollowBigChanges(followBigChanges, updateFlag);
			setSpatialFiltering(spatialFiltering, updateFlag);

			int nAvg = numAveragingSlots;
			kinectgrabber.performInThread([nAvg](KinectGrabber& kg) { kg.setAveragingSlotsNumber(nAvg); });

			updateFlag ? updateStatusGUI() : noop;
		}
		else
		{
			ofLogVerbose("KinectProjector") << "KinectProjector.setup(): Settings could not be loaded";
			updateErrorEvent("SETTINGS_NOT_LOADED");
			return "SETTINGS_NOT_LOADED";
		}
	}

	setFullCalibState(FULL_CALIBRATION_STATE_DONE);
	setROICalibState(ROI_CALIBRATION_STATE_DONE);
	setAutoCalibrationState(AUTOCALIB_STATE_DONE);
	return "";

}


string KinectProjector::startApplication(bool updateFlag = true)
{
	
	if (GetApplicationState() == APPLICATION_STATE_RUNNING)
	{
		setApplicationState(APPLICATION_STATE_SETUP);
		updateStateEvent();
		updateFlag ? updateStatusGUI() : noop;
		return "";
	}
	if (GetApplicationState() == APPLICATION_STATE_CALIBRATING)
	{
		ofLogVerbose("KinectProjector") << "KinectProjector.startApplication() : we are calibrating";
		updateErrorEvent("CALIBRATING");
		return "CALIBRATING";
	}
	
	string message = checkStartReady(updateFlag);
	if (!message.empty()) {
		return message;
	}
	
	ResetSeaLevel();

	// If all is well we are running
	setApplicationState(APPLICATION_STATE_RUNNING);
	
	drawKinectColorView = false;
	drawKinectView = false;
	if (updateFlag) {
		gui->getToggle(CMP_DRAW_KINECT_COLOR_VIEW)->setChecked(drawKinectColorView);
		gui->getToggle(CMP_DRAW_KINECT_DEPTH_VIEW)->setChecked(drawKinectView);
		updateStatusGUI();
	}
	updateStateEvent();
	return "";
}

void KinectProjector::startFullCalibration()
{
	if (!kinectOpened)
	{
		ofLogVerbose("KinectProjector") << "startFullCalibration(): Kinect not running";
		return;
	}
	if (GetApplicationState() == APPLICATION_STATE_CALIBRATING)
	{
		ofLogVerbose("KinectProjector") << "startFullCalibration(): we are already calibrating";
		return;
	}

	setApplicationState(APPLICATION_STATE_CALIBRATING);
	setCalibrationState(CALIBRATION_STATE_FULL_AUTO_CALIBRATION);
	setFullCalibState(FULL_CALIBRATION_STATE_ROI_DETERMINATION);
	setROICalibState(ROI_CALIBRATION_STATE_INIT);
	confirmModal->setTitle("Full calibration");
	calibModal->setTitle("Full calibration");
	askToFlattenSand();
	ofLogVerbose("KinectProjector") << "startFullCalibration(): Starting full calibration";
	updateStatusGUI();
	updateStateEvent();
}

void KinectProjector::startAutomaticROIDetection()
{
	setApplicationState(APPLICATION_STATE_CALIBRATING);
	setCalibrationState(CALIBRATION_STATE_ROI_AUTO_DETERMINATION);
	setROICalibState(ROI_CALIBRATION_STATE_INIT);
	ofLogVerbose("KinectProjector") << "onButtonEvent(): Finding ROI";
	confirmModal->setTitle("Detect sand region");
	calibModal->setTitle("Detect sand region");
	askToFlattenSand();
	ofLogVerbose("KinectProjector") << "startAutomaticROIDetection(): starting ROI detection";
	updateStatusGUI();
	updateStateEvent();
}

void KinectProjector::startAutomaticKinectProjectorCalibration()
{
	startAutomaticKinectProjectorCalibration(true);
}

string KinectProjector::startAutomaticKinectProjectorCalibration(bool updateGui = true)
{
	if (!kinectOpened)
	{
		ofLogVerbose("KinectProjector") << "startAutomaticKinectProjectorCalibration(): Kinect not running";
		updateErrorEvent("KINECT_NOT_RUNNING");
		return "KINECT_NOT_RUNNING";
	}
	if (GetApplicationState() == APPLICATION_STATE_CALIBRATING)
	{
		setApplicationState(APPLICATION_STATE_SETUP);
		setConfirmModalState(CONFIRM_MODAL_CLOSED);
		calibrationText = "Terminated before completion";
		updateGui ? updateStatusGUI() : noop;
		updateStateEvent();
		updateErrorEvent("ALREADY_CALIBRATING");
		return "ALREADY_CALIBRATING";
	}
	if (!ROIcalibrated)
	{
		ofLogVerbose("KinectProjector") << "startAutomaticKinectProjectorCalibration(): ROI not defined";
		updateErrorEvent("ROI_NOT_DEFINED");
		return "ROI_NOT_DEFINED";
	}

	calibrationText = "Starting projector/kinect calibration";
	setApplicationState(APPLICATION_STATE_CALIBRATING);
	setCalibrationState(CALIBRATION_STATE_PROJ_KINECT_AUTO_CALIBRATION);
	autoCalibState = AUTOCALIB_STATE_INIT_POINT;
	confirmModal->setTitle("Calibrate projector");
	calibModal->setTitle("Calibrate projector");
	askToFlattenSandFlag = true;
	ofLogVerbose("KinectProjector") << "startAutomaticKinectProjectorCalibration(): Starting autocalib";
	updateGui ? updateStatusGUI() : noop;
	updateStateEvent();
	return "";
}

void KinectProjector::setSpatialFiltering(bool sspatialFiltering, bool updateGui = true)
{
	spatialFiltering = sspatialFiltering;
	kinectgrabber.performInThread([sspatialFiltering](KinectGrabber &kg) {
		kg.setSpatialFiltering(sspatialFiltering);
	});
	if (updateGui)
	{
		updateStatusGUI();
	}
	updateStateEvent();
}

bool KinectProjector::getSpatialFiltering()
{
	return spatialFiltering;
}

void KinectProjector::setInPainting(bool inp, bool updateGui = true)
{
	doInpainting = inp;
	kinectgrabber.performInThread([inp](KinectGrabber &kg) {
		kg.setInPainting(inp);
	});
	if (updateGui)
	{
		updateStatusGUI();
	}
	updateStateEvent();
}

bool KinectProjector::getInPainting()
{
	return doInpainting;
}

void KinectProjector::setFullFrameFiltering(bool ff, bool updateGui = true)
{
	doFullFrameFiltering = ff;
	ofRectangle ROI = kinectROI;
	kinectgrabber.performInThread([ff, ROI](KinectGrabber &kg) {
		kg.setFullFrameFiltering(ff, ROI);
	});
	if (updateGui)
	{
		updateStatusGUI();
	}
	updateStateEvent();
}

bool KinectProjector::getFullFrameFiltering()
{
	return doFullFrameFiltering;
}

void KinectProjector::setFollowBigChanges(bool sfollowBigChanges, bool updateGui = true)
{
	followBigChanges = sfollowBigChanges;
	kinectgrabber.performInThread([sfollowBigChanges](KinectGrabber &kg) {
		kg.setFollowBigChange(sfollowBigChanges);
	});
	if (updateGui)
	{
		updateStatusGUI();
	}
}

bool KinectProjector::getFollowBigChanges()
{
	return followBigChanges;
}

void KinectProjector::onButtonEvent(ofxDatGuiButtonEvent e)
{
	if (e.target->is("Full Calibration"))
	{
		startFullCalibration();
	}
	else if (e.target->is("Start Application"))
	{
		startApplication(true);
	}
	else if (e.target->is("Update ROI from calibration"))
	{
		updateROIFromCalibration();
	}
	else if (e.target->is("Automatically detect sand region"))
	{
		startAutomaticROIDetection();
	}
	else if (e.target->is("Manually define sand region"))
	{
		StartManualROIDefinition();
	}
	else if (e.target->is("Automatically calibrate kinect & projector"))
	{
		startAutomaticKinectProjectorCalibration(true);
	}
	else if (e.target->is("Manually calibrate kinect & projector"))
	{
		// Not implemented yet
	}
	else if (e.target->is("Reset sea level"))
	{
		ResetSeaLevel();
	}
	else if (e.target->is("Auto Adjust ROI"))
	{
		updateROIFromCalibration();
	}
}

void KinectProjector::StartManualROIDefinition()
{
	setCalibrationState(CALIBRATION_STATE_ROI_MANUAL_DETERMINATION);
	setROICalibState(ROI_CALIBRATION_STATE_INIT);
	ROIStartPoint.x = -1;
	ROIStartPoint.y = -1;
	calibrationText = "Manually defining sand region";
	updateStatusGUI();
}

void KinectProjector::ResetSeaLevel()
{
	gui->getSlider(CMP_TILT_X)->setValue(0);
	gui->getSlider(CMP_TILT_Y)->setValue(0);
	gui->getSlider(CMP_VERTICAL_OFFSET)->setValue(0);
	basePlaneNormal = basePlaneNormalBack;
	basePlaneOffset = basePlaneOffsetBack;
	basePlaneEq = getPlaneEquation(basePlaneOffset, basePlaneNormal);
	basePlaneUpdated = true;
	updateStateEvent();
}

void KinectProjector::showROIonProjector(bool show)
{
	doShowROIonProjector = show;
	updateStateEvent();
}

bool KinectProjector::getShowROIonProjector()
{
	return doShowROIonProjector;
}

void KinectProjector::setDumpDebugFiles(bool value)
{
	DumpDebugFiles = value;
	updateStateEvent();
}

void KinectProjector::setAutoCalibrationState(Auto_calibration_state newValue)
{
	auto oldvalue = autoCalibState;
	autoCalibState = newValue;
	if (oldvalue != newValue && broadcastState) {
		updateStateEvent();
	}
}

void KinectProjector::setCalibrationState(Calibration_state newValue) {	
	auto oldvalue = calibrationState;
	calibrationState = newValue;
	if (oldvalue != newValue && broadcastState) {
		updateStateEvent();
	}
}


void KinectProjector::setFullCalibState(Full_Calibration_state newValue) {
	auto oldvalue = fullCalibState;
	fullCalibState = newValue;
	if (oldvalue != newValue && broadcastState) {
		updateStateEvent();
	}
}

void KinectProjector::setApplicationState(Application_state newValue) {
	auto oldvalue = applicationState;
	applicationState = newValue;
	if (oldvalue != newValue && broadcastState) {
		updateStateEvent();
	}
}

void KinectProjector::setROICalibState(ROI_calibration_state newValue) {
	auto oldvalue = ROICalibState;
	ROICalibState = newValue;
	if (oldvalue != newValue && broadcastState) {
		updateStateEvent();
	}
}

void KinectProjector::setConfirmModalMessage(string message)
{
	auto oldvalue = confirmModalMessage;
	confirmModalMessage = message;
	
	if (oldvalue.compare(confirmModalMessage)!=0 && broadcastState) {
		cout << "setConfirmModalMessage " << endl;
		confirmModal->setMessage(message);
		updateStateEvent();
	}
}

void KinectProjector::setConfirmModalState(ConfirmModal_State state)
{
	auto oldvalue = confirmModalState;
	confirmModalState = state;
	if (oldvalue != state && broadcastState) {
		cout << "setConfirmModalState " << endl;
		updateStateEvent();
		if (confirmModalState == CONFIRM_MODAL_CLOSED) {
			confirmModal->hide();
		}
		if (confirmModalState == CONFIRM_MODAL_OPENED) {
			confirmModal->show();
		}
		updateStateEvent();
	}
}

void KinectProjector::setProjKinectCalibrated(bool newValue)
{
	auto oldvalue = projKinectCalibrated;
	projKinectCalibrated = newValue;
	if (oldvalue != newValue && broadcastState) {
		updateStateEvent();
	}
}


bool KinectProjector::getDumpDebugFiles()
{
	return DumpDebugFiles;
}

void KinectProjector::onToggleEvent(ofxDatGuiToggleEvent e)
{
	(e.target->is(CMP_SPATIAL_FILTERING)) ? setSpatialFiltering(e.checked) : (e.target->is(CMP_QUICK_REACTION)) ? setFollowBigChanges(e.checked) : (e.target->is(CMP_INPAINT_OUTLIERS)) ? setInPainting(e.checked) : (e.target->is(CMP_FULL_FRAME_FILTERING)) ? setFullFrameFiltering(e.checked) : (e.target->is(CMP_DRAW_KINECT_DEPTH_VIEW)) ? setDrawKinectDepthView(e.checked) : (e.target->is(CMP_DRAW_KINECT_COLOR_VIEW)) ? setDrawKinectColorView(e.checked) : (e.target->is(CMP_DUMP_DEBUG)) ? setDumpDebugFiles(e.checked) : (e.target->is(CMP_SHOW_ROI_ON_SAND)) ? showROIonProjector(e.checked) : noop;
}

void KinectProjector::setAveraging(float value)
{
	numAveragingSlots = value;
	kinectgrabber.performInThread([value](KinectGrabber &kg) {
		kg.setAveragingSlotsNumber(value);
	});
	updateStateEvent();
}

void KinectProjector::setDrawKinectDepthView(bool value)
{
	drawKinectView = value;
	if (drawKinectView)
	{
		drawKinectColorView = false;
		gui->getToggle(CMP_DRAW_KINECT_COLOR_VIEW)->setChecked(drawKinectColorView);
	}
	updateStateEvent();
}

bool KinectProjector::getDrawKinectDepthView()
{
	return drawKinectView;
}

void KinectProjector::setDrawKinectColorView(bool value)
{
	drawKinectColorView = value;
	if (drawKinectColorView)
	{
		drawKinectView = false;
		gui->getToggle(CMP_DRAW_KINECT_DEPTH_VIEW)->setChecked(drawKinectView);
	}
	updateStateEvent();
}


bool KinectProjector::getDrawKinectColorView()
{
	return drawKinectColorView;
}

int KinectProjector::getAveraging()
{
	return numAveragingSlots;
}

void KinectProjector::setCeiling(float value)
{
	cout << "setCeiling " << value << endl;
	cout << "maxOffsetBack " << maxOffsetBack << endl;
	maxOffset = maxOffsetBack - value;
	cout << "onSliderEvent(): maxOffset " << maxOffset << endl;
	kinectgrabber.performInThread([this](KinectGrabber &kg) {
		kg.setMaxOffset(this->maxOffset);
	});
	updateStateEvent();
}

float KinectProjector::getMaxOffset(){
	return maxOffsetBack - maxOffset;
}

float KinectProjector::getCeiling()
{
	return maxOffset - maxOffsetBack;
}

void KinectProjector::setTilt(float tiltX, float tiltY)
{
	//basePlaneNormal = basePlaneNormalBack.getRotated(gui->getSlider("Tilt X")->getValue(), ofVec3f(1, 0, 0));
	//basePlaneNormal.rotate(gui->getSlider("Tilt Y")->getValue(), ofVec3f(0, 1, 0));
	basePlaneNormal = basePlaneNormalBack.getRotated(tiltX, ofVec3f(1, 0, 0));
	basePlaneNormal.rotate(tiltY, ofVec3f(0, 1, 0));
	basePlaneEq = getPlaneEquation(basePlaneOffset, basePlaneNormal);
	basePlaneUpdated = true;
	updateStateEvent();
}

void KinectProjector::setTiltX(float value)
{
	tiltX = value;
	setTilt(tiltX, tiltY);
}

void KinectProjector::setTiltY(float value)
{
	tiltY = value;
	setTilt(tiltX, tiltY);
}

float KinectProjector::getTiltX()
{
	return tiltX;
}

float KinectProjector::getTiltY()
{
	return tiltY;
}

void KinectProjector::setVerticalOffset(float value)
{
	verticalOffset = value;
	basePlaneOffset.z = basePlaneOffsetBack.z + verticalOffset;
	basePlaneEq = getPlaneEquation(basePlaneOffset, basePlaneNormal);
	basePlaneUpdated = true;
	updateStateEvent();
}

float KinectProjector::getVerticalOffset()
{
	return verticalOffset;
}

void KinectProjector::onSliderEvent(ofxDatGuiSliderEvent e)
{
	e.target->is(CMP_VERTICAL_OFFSET) ? setVerticalOffset(e.value) : 
	e.target->is(CMP_TILT_X) ? setTiltX(e.value) : 
	e.target->is(CMP_TILT_Y) ? setTiltY(e.value) : 
	e.target->is(CMP_CEILING) ? setCeiling(e.value) : 
	e.target->is(CMP_AVERAGING) ? setAveraging(e.value) : noop;
}

void KinectProjector::onConfirmModalEvent(ofxModalEvent e)
{
	cout << "onConfirmModalEvent " << e.type << endl;
	if (e.type == ofxModalEvent::SHOWN) {
		ofLogVerbose("KinectProjector") << "Confirm modal window is open";
		setConfirmModalState(CONFIRM_MODAL_OPENED);
	} else 
	if (e.type == ofxModalEvent::HIDDEN) {
		if (!kinectOpened) {
			setConfirmModalMessage("STILL_NO_CONNECTION_KINECT");
			// confirmModal->setMessage("Still no connection to Kinect. Please check that the kinect is (1) connected, (2) powerer and (3) not used by another application.");
			setConfirmModalState(CONFIRM_MODAL_OPENED);
		} else {
			setConfirmModalState(CONFIRM_MODAL_CLOSED);
		}
		ofLogVerbose("KinectProjector") << "Confirm modal window is closed";
	} else
	if (e.type == ofxModalEvent::CANCEL) {
		setConfirmModalState(CONFIRM_MODAL_CLOSED);
		onCancelCalibration(true);
	} else
	if (e.type == ofxModalEvent::CONFIRM) {
		onConfirmCalibration();
	}
	updateStateEvent();
}

string KinectProjector::onCancelCalibration(bool updateGui = true)
{
	setApplicationState(APPLICATION_STATE_SETUP);
	ofLogVerbose("KinectProjector") << "Modal cancel button pressed: Aborting";
	setConfirmModalState(CONFIRM_MODAL_CLOSED);
	if (updateGui)
	{
		updateStatusGUI();
	}
	return "";
}

string KinectProjector::onConfirmCalibration()
{
	setConfirmModalState(CONFIRM_MODAL_CLOSED);
	if (GetApplicationState() == APPLICATION_STATE_CALIBRATING) {
		if (waitingForFlattenSand) {
			waitingForFlattenSand = false;
			calibModal->hide();
		} else
		if ((GetCalibrationState() == CALIBRATION_STATE_PROJ_KINECT_AUTO_CALIBRATION ||
			(GetCalibrationState() == CALIBRATION_STATE_FULL_AUTO_CALIBRATION && GetFullCalibState() == FULL_CALIBRATION_STATE_AUTOCALIB)) &&
			GetAutoCalibrationState() == AUTOCALIB_STATE_NEXT_POINT)
		{
			if (!upframe) {
				upframe = true;
			}
		}
	}
	ofLogVerbose("KinectProjector") << "Modal confirm button pressed";
	return "";
}

void KinectProjector::onCalibModalEvent(ofxModalEvent e)
{
	if (e.type == ofxModalEvent::SHOWN)
	{
		ofLogVerbose("KinectProjector") << "calib modal window is open";
	}
	else if (e.type == ofxModalEvent::HIDDEN)
	{
		ofLogVerbose("KinectProjector") << "calib modal window is closed";
	}
	else if (e.type == ofxModalEvent::CONFIRM)
	{
		setApplicationState(APPLICATION_STATE_SETUP);
		ofLogVerbose("KinectProjector") << "Modal cancel button pressed: Aborting";
		updateStatusGUI();
	}
	updateStateEvent();
}

void KinectProjector::saveCalibrationAndSettings()
{
	if (isCalibrated())
	{
		if (kpt->saveCalibration("settings/calibration.xml"))
		{
			ofLogVerbose("KinectProjector") << "update(): initialisation: Calibration saved ";
		}
		else
		{
			ofLogVerbose("KinectProjector") << "update(): initialisation: Calibration could not be saved ";
		}
	}
	if (ROIcalibrated)
	{
		if (saveSettings())
		{
			ofLogVerbose("KinectProjector") << "update(): initialisation: Settings saved ";
		}
		else
		{
			ofLogVerbose("KinectProjector") << "update(): initialisation: Settings could not be saved ";
		}
	}
}

bool KinectProjector::loadSettings()
{
	string settingsFile = "settings/kinectProjectorSettings.xml";

	ofXml xml;
	if (!xml.load(settingsFile))
		return false;
	xml.setTo("KINECTSETTINGS");
	kinectROI = xml.getValue<ofRectangle>("kinectROI");
	basePlaneNormalBack = xml.getValue<ofVec3f>("basePlaneNormalBack");
	basePlaneNormal = basePlaneNormalBack;
	basePlaneOffsetBack = xml.getValue<ofVec3f>("basePlaneOffsetBack");
	basePlaneOffset = basePlaneOffsetBack;
	basePlaneEq = xml.getValue<ofVec4f>("basePlaneEq");
	maxOffsetBack = xml.getValue<float>("maxOffsetBack");
	maxOffset = maxOffsetBack;
	spatialFiltering = xml.getValue<bool>("spatialFiltering");
	followBigChanges = xml.getValue<bool>("followBigChanges");
	numAveragingSlots = xml.getValue<int>("numAveragingSlots");
	doInpainting = xml.getValue<bool>("OutlierInpainting", false);
	doFullFrameFiltering = xml.getValue<bool>("FullFrameFiltering", false);
	return true;
}

bool KinectProjector::saveSettings()
{
	string settingsFile = "settings/kinectProjectorSettings.xml";

	ofXml xml;
	xml.addChild("KINECTSETTINGS");
	xml.setTo("KINECTSETTINGS");
	xml.addValue("kinectROI", kinectROI);
	xml.addValue("basePlaneNormalBack", basePlaneNormalBack);
	xml.addValue("basePlaneOffsetBack", basePlaneOffsetBack);
	xml.addValue("basePlaneEq", basePlaneEq);
	xml.addValue("maxOffsetBack", maxOffsetBack);
	xml.addValue("spatialFiltering", spatialFiltering);
	xml.addValue("followBigChanges", followBigChanges);
	xml.addValue("numAveragingSlots", numAveragingSlots);
	xml.addValue("OutlierInpainting", doInpainting);
	xml.addValue("FullFrameFiltering", doFullFrameFiltering);
	xml.setToParent();
	return xml.save(settingsFile);
}

void KinectProjector::ProcessChessBoardInput(ofxCvGrayscaleImage &image)
{
	CheckAndNormalizeKinectROI();

	unsigned char *imgD = image.getPixels().getData();
	unsigned char minV = 255;
	unsigned char maxV = 0;

	// Find min and max values inside ROI
	for (int y = kinectROI.getMinY(); y < kinectROI.getMaxY(); y++)
	{
		for (int x = kinectROI.getMinX(); x < kinectROI.getMaxX(); x++)
		{
			int idx = y * image.width + x;
			unsigned char val = imgD[idx];

			if (val > maxV)
				maxV = val;
			if (val < minV)
				minV = val;
		}
	}
	std::cout << "Min " << (int)minV << " max " << (int)maxV << std::endl;
	double scale = 255.0 / (maxV - minV);

	for (int y = 0; y < image.height; y++)
	{
		for (int x = 0; x < image.width; x++)
		{
			int idx = y * image.width + x;
			unsigned char val = imgD[idx];
			double newVal = (val - minV) * scale;
			newVal = std::min(newVal, 255.0);
			newVal = std::max(newVal, 0.0);

			imgD[idx] = (unsigned char)newVal;
		}
	}
}

void KinectProjector::CheckAndNormalizeKinectROI()
{
	bool fixed = false;
	if (kinectROI.x < 0)
	{
		fixed = true;
		kinectROI.x = 0;
	}
	if (kinectROI.y < 0)
	{
		fixed = true;
		kinectROI.y = 0;
	}
	if (kinectROI.x + kinectROI.width >= kinectRes.x)
	{
		fixed = true;
		kinectROI.width = kinectRes.x - 1 - kinectROI.x;
	}
	if (kinectROI.y + kinectROI.height >= kinectRes.y)
	{
		fixed = true;
		kinectROI.height = kinectRes.y - 1 - kinectROI.y;
	}

	if (fixed)
		ofLogVerbose("KinectProjector") << "CheckAndNormalizeKinectROI(): Kinect ROI fixed since it was out of bounds";
}

void KinectProjector::SaveFilteredDepthImageDebug()
{
	std::string rawValOutKC = ofToDataPath(DebugFileOutDir + "RawValsKinectCoords.txt");
	std::string rawValOutWC = ofToDataPath(DebugFileOutDir + "RawValsWorldCoords.txt");
	std::string rawValOutHM = ofToDataPath(DebugFileOutDir + "RawValsHM.txt");
	std::string BinOutName = DebugFileOutDir + "RawBinImg.png";
	std::string DepthOutName = DebugFileOutDir + "RawDepthImg.png";

	std::ofstream fostKC(rawValOutKC.c_str());
	std::ofstream fostWC(rawValOutWC.c_str());
	std::ofstream fostHM(rawValOutHM.c_str());

	ofxCvFloatImage temp;
	temp.setFromPixels(FilteredDepthImage.getFloatPixelsRef().getData(), kinectRes.x, kinectRes.y);
	temp.setNativeScale(FilteredDepthImage.getNativeScaleMin(), FilteredDepthImage.getNativeScaleMax());
	temp.convertToRange(0, 1);
	ofxCvGrayscaleImage temp2;
	temp2.setFromPixels(temp.getFloatPixelsRef());
	ofSaveImage(temp2.getPixels(), DepthOutName);

	float *imgData = FilteredDepthImage.getFloatPixelsRef().getData();

	ofxCvGrayscaleImage BinImg;
	BinImg.allocate(kinectRes.x, kinectRes.y);
	unsigned char *binData = BinImg.getPixels().getData();

	for (int y = 0; y < kinectRes.y; y++)
	{
		for (int x = 0; x < kinectRes.x; x++)
		{
			int IDX = y * kinectRes.x + x;
			double val = imgData[IDX];

			fostKC << val << std::endl;

			// Kinect coords
			ofVec4f kc = ofVec4f(x, y, val, 1);

			// World coords
			ofVec4f wc = kinectWorldMatrix * kc * kc.z;
			fostWC << wc.x << " " << wc.y << " " << wc.z << std::endl;

			float H = elevationAtKinectCoord(x, y);
			fostHM << H << std::endl;

			unsigned char BinOut = H > 0;

			binData[IDX] = BinOut;
		}
	}

	ofSaveImage(BinImg.getPixels(), BinOutName);
}

bool KinectProjector::getBinaryLandImage(ofxCvGrayscaleImage &BinImg)
{
	if (!kinectOpened)
		return false;

	float *imgData = FilteredDepthImage.getFloatPixelsRef().getData();

	BinImg.allocate(kinectRes.x, kinectRes.y);
	unsigned char *binData = BinImg.getPixels().getData();

	for (int y = 0; y < kinectRes.y; y++)
	{
		for (int x = 0; x < kinectRes.x; x++)
		{
			int IDX = y * kinectRes.x + x;
			double val = imgData[IDX];

			float H = elevationAtKinectCoord(x, y);

			unsigned char BinOut = 255 * (H > 0);

			binData[IDX] = BinOut;
		}
	}

	return true;
}

ofRectangle KinectProjector::getProjectorActiveROI()
{
	ofRectangle projROI = ofRectangle(ofPoint(0, 0), ofPoint(projRes.x, projRes.y));

	//if (kinectOpened)
	//{

	//	ofVec2f a = worldCoordTokinectCoord(projCoordAndWorldZToWorldCoord(0, 0, basePlaneOffset.z));
	//	ofVec2f b = worldCoordTokinectCoord(projCoordAndWorldZToWorldCoord(projRes.x, 0, basePlaneOffset.z));
	//	ofVec2f c = worldCoordTokinectCoord(projCoordAndWorldZToWorldCoord(projRes.x, projRes.y, basePlaneOffset.z));
	//	ofVec2f d = worldCoordTokinectCoord(projCoordAndWorldZToWorldCoord(0, projRes.y, basePlaneOffset.z));
	//	float x1 = max(a.x, d.x);
	//	float x2 = min(b.x, c.x);
	//	float y1 = max(a.y, b.y);
	//	float y2 = min(c.y, d.y);

	//	ofVec2f UL = kinectCoordToProjCoord(x1, y1, basePlaneOffset.z);
	//	ofVec2f LR = kinectCoordToProjCoord(x2, y2, basePlaneOffset.z);
	//	projROI = ofRectangle(ofPoint(UL.x, UL.y), ofPoint(LR.x, LR.y));
	//	projROI.standardize();
	//}

	return projROI;
}

void KinectProjector::SaveFilteredDepthImage()
{
	std::string rawValOutKC = ofToDataPath(DebugFileOutDir + "RawValsKinectCoords.txt");
	std::string rawValOutWC = ofToDataPath(DebugFileOutDir + "RawValsWorldCoords.txt");
	std::string rawValOutHM = ofToDataPath(DebugFileOutDir + "RawValsHM.txt");
	std::string BinOutName = DebugFileOutDir + "RawBinImg.png";
	std::string DepthOutName = DebugFileOutDir + "RawDepthImg.png";

	std::ofstream fostKC(rawValOutKC.c_str());
	std::ofstream fostWC(rawValOutWC.c_str());
	std::ofstream fostHM(rawValOutHM.c_str());

	ofxCvFloatImage temp;
	temp.setFromPixels(FilteredDepthImage.getFloatPixelsRef().getData(), kinectRes.x, kinectRes.y);
	temp.setNativeScale(FilteredDepthImage.getNativeScaleMin(), FilteredDepthImage.getNativeScaleMax());
	temp.convertToRange(0, 1);
	ofxCvGrayscaleImage temp2;
	temp2.setFromPixels(temp.getFloatPixelsRef());
	ofSaveImage(temp2.getPixels(), DepthOutName);

	float *imgData = FilteredDepthImage.getFloatPixelsRef().getData();

	ofxCvGrayscaleImage BinImg;
	BinImg.allocate(kinectRes.x, kinectRes.y);
	unsigned char *binData = BinImg.getPixels().getData();

	for (int y = 0; y < kinectRes.y; y++)
	{
		for (int x = 0; x < kinectRes.x; x++)
		{
			int IDX = y * kinectRes.x + x;
			double val = imgData[IDX];

			fostKC << val << std::endl;

			// Kinect coords
			ofVec4f kc = ofVec4f(x, y, val, 1);

			// World coords
			ofVec4f wc = kinectWorldMatrix * kc * kc.z;
			fostWC << wc.x << " " << wc.y << " " << wc.z << std::endl;

			float H = elevationAtKinectCoord(x, y);
			fostHM << H << std::endl;

			unsigned char BinOut = H > 0;

			binData[IDX] = BinOut;
		}
	}

	ofSaveImage(BinImg.getPixels(), BinOutName);
}

void KinectProjector::SaveKinectColorImage()
{
	std::string ColourOutName = DebugFileOutDir + "RawColorImage.png";
	std::string MedianOutName = DebugFileOutDir + "TemporalFilteredImage.png";
	ofSaveImage(kinectColorImage.getPixels(), ColourOutName);

	if (TemporalFrameFilter.isValid())
	{
		ofxCvGrayscaleImage tempImage;
		//		tempImage.allocate(kinectColorImage.width, kinectColorImage.height);
		if (TemporalFilteringType == 0)
			tempImage.setFromPixels(TemporalFrameFilter.getMedianFilteredImage(), kinectColorImage.width, kinectColorImage.height);
		if (TemporalFilteringType == 1)
			tempImage.setFromPixels(TemporalFrameFilter.getAverageFilteredColImage(), kinectColorImage.width, kinectColorImage.height);
		ofSaveImage(tempImage.getPixels(), MedianOutName);
	}
}

static void removeCRLF(string &targetStr)
{
	const char CR = '\r';
	const char LF = '\n';
	string str;
	for (const auto c : targetStr)
	{
		if (c != CR && c != LF)
		{
			str += c;
		}
	}
	targetStr = std::move(str);
}
static string base64_encode(ofBuffer buffer)
{
	stringstream ss;
	ss.str("");
	Poco::Base64Encoder encoder(ss);
	encoder << buffer;
	encoder.close();
	string str = ss.str();
	removeCRLF(str);
	return str;
}

string KinectProjector::getKinectColorImage()
{
	ofPixels pixels = kinectColorImage.getPixels();
	ofBuffer imageBuffer;
	ofSaveImage(pixels, imageBuffer);
	return base64_encode(imageBuffer);
}

ofxDatGui *KinectProjector::getGui()
{
	return gui;
}

void KinectProjector::updateStateEvent()
{
	updateStateEvent(true);
}

void KinectProjector::updateStateEvent(bool value)
{
	stateEvent = value;
}

bool KinectProjector::isUpdateStateEvent() {
	return stateEvent;
}


void KinectProjector::updateErrorEvent(string error) {
	errorEvent = error;
}

string KinectProjector::getErrorEvent() {
	return errorEvent;
}




//void KinectProjector::setBroadcastMethod(std::function<void(Json::Value)> method)
//{
//	broadcast = method;
//}

//void KinectProjector::setBroadcastStateMethod(std::function<void()> fn)
//{
//	broadcastState = fn;
//}
