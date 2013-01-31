#include "testApp.h"

// from ProCamToolkit
GLdouble modelviewMatrix[16], projectionMatrix[16];
GLint viewport[4];
void updateProjectionState() {
	glGetDoublev(GL_MODELVIEW_MATRIX, modelviewMatrix);
	glGetDoublev(GL_PROJECTION_MATRIX, projectionMatrix);
	glGetIntegerv(GL_VIEWPORT, viewport);
}

ofVec3f ofWorldToScreen(ofVec3f world) {
	updateProjectionState();
	GLdouble x, y, z;
	gluProject(world.x, world.y, world.z, modelviewMatrix, projectionMatrix, viewport, &x, &y, &z);
	ofVec3f screen(x, y, z);
	screen.y = ofGetHeight() - screen.y;
	return screen;
}

ofMesh getProjectedMesh(const ofMesh& mesh) {
	ofMesh projected = mesh;
	for(int i = 0; i < mesh.getNumVertices(); i++) {
		ofVec3f cur = ofWorldToScreen(mesh.getVerticesPointer()[i]);
		cur.z = 0;
		projected.setVertex(i, cur);
	}
	return projected;
}

template <class T>
void addTexCoords(ofMesh& to, const vector<T>& from) {
	for(int i = 0; i < from.size(); i++) {
		to.addTexCoord(from[i]);
	}
}

using namespace ofxCv;

void testApp::setup() {
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofSetDrawBitmapMode(OF_BITMAPMODE_MODEL_BILLBOARD);
	cam.initGrabber(640, 480);
	
	tracker.setup();
	eyeFbo.allocate(128, 48, GL_RGB);
	runningMean = 24;
	
	//osc.setup("localhost", 8338);
    lampIsOn = false;
    onDuration=0;
    offDuration=0;
    baseBrightness=1000;
    baseUnitOfTime=500;
    currentMorse="";
    translation="";
    morseSentence="";
    loadMorseFromFile();
    PBlink=0;
    numBlinks=0;
    bufferPos=0;
    for(int i=0;i<5;i++){
        smoothedBlinks.push_back(0.0f);
    }
}

void testApp::update() {
	cam.update();
	if(cam.isFrameNew()) {
		tracker.update(toCv(cam));
		position = tracker.getPosition();
		scale = tracker.getScale();
		rotationMatrix = tracker.getRotationMatrix();
		
		if(tracker.getFound()) {
			ofVec2f
			leftOuter = tracker.getImagePoint(36),
			leftInner = tracker.getImagePoint(39),
			rightInner = tracker.getImagePoint(42),
			rightOuter = tracker.getImagePoint(45);
			
			ofPolyline leftEye = tracker.getImageFeature(ofxFaceTracker::LEFT_EYE);
			ofPolyline rightEye = tracker.getImageFeature(ofxFaceTracker::RIGHT_EYE);
			
			ofVec2f leftCenter = leftEye.getBoundingBox().getCenter();
			ofVec2f rightCenter = rightEye.getBoundingBox().getCenter();
			
			float leftRadius = (leftCenter.distance(leftInner) + leftCenter.distance(leftOuter)) / 2;
			float rightRadius = (rightCenter.distance(rightInner) + rightCenter.distance(rightOuter)) / 2;
			
			ofVec2f
			leftOuterObj = tracker.getObjectPoint(36),
			leftInnerObj = tracker.getObjectPoint(39),
			rightInnerObj = tracker.getObjectPoint(42),
			rightOuterObj = tracker.getObjectPoint(45);
			
			ofVec3f upperBorder(0, -3.5, 0), lowerBorder(0, 2.5, 0);
			ofVec3f leftDirection(-1, 0, 0), rightDirection(+1, 0, 0);
			float innerBorder = 1.5, outerBorder = 2.5;
			
			ofMesh leftRect, rightRect;
			leftRect.setMode(OF_PRIMITIVE_LINE_LOOP);
			leftRect.addVertex(leftOuterObj + upperBorder + leftDirection * outerBorder);
			leftRect.addVertex(leftInnerObj + upperBorder + rightDirection * innerBorder);
			leftRect.addVertex(leftInnerObj + lowerBorder + rightDirection * innerBorder);
			leftRect.addVertex(leftOuterObj + lowerBorder + leftDirection * outerBorder);
			rightRect.setMode(OF_PRIMITIVE_LINE_LOOP);
			rightRect.addVertex(rightInnerObj+ upperBorder + leftDirection * innerBorder);
			rightRect.addVertex(rightOuterObj + upperBorder + rightDirection * outerBorder);
			rightRect.addVertex(rightOuterObj + lowerBorder + rightDirection * outerBorder);
			rightRect.addVertex(rightInnerObj + lowerBorder + leftDirection * innerBorder);
			
			ofPushMatrix();
			ofSetupScreenOrtho(640, 480, OF_ORIENTATION_UNKNOWN, true, -1000, 1000);
			ofScale(1, 1, -1);
			ofTranslate(position);
			applyMatrix(rotationMatrix);
			ofScale(scale, scale, scale);
			leftRectImg = getProjectedMesh(leftRect);
			rightRectImg = getProjectedMesh(rightRect);		
			ofPopMatrix();
			
			// more effective than using object space points would be to use image space
			// but translate to the center of the eye and orient the rectangle in the
			// direction the face is facing.
			/*
			 ofPushMatrix();
			 ofTranslate(tracker.getImageFeature(ofxFaceTracker::LEFT_EYE).getCentroid2D());
			 applyMatrix(rotationMatrix);
			 ofRect(-50, -40, 2*50, 2*40);
			 ofPopMatrix();
			 
			 ofPushMatrix();
			 ofTranslate(tracker.getImageFeature(ofxFaceTracker::RIGHT_EYE).getCentroid2D());
			 applyMatrix(rotationMatrix);
			 ofRect(-50, -40, 2*50, 2*40);
			 ofPopMatrix();
			 */
			
			ofMesh normRect, normLeft, normRight;
			normRect.addVertex(ofVec2f(0, 0));
			normRect.addVertex(ofVec2f(64, 0));
			normRect.addVertex(ofVec2f(64, 48));
			normRect.addVertex(ofVec2f(0, 48));
			normLeft.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
			normRight.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
			normLeft.addVertices(normRect.getVertices());
			normRight.addVertices(normRect.getVertices());
			addTexCoords(normLeft, leftRectImg.getVertices());
			addTexCoords(normRight, rightRectImg.getVertices());
			
			eyeFbo.begin();
			ofSetColor(255);
			ofFill();
			cam.getTextureReference().bind();
			normLeft.draw();
			ofTranslate(64, 0);
			normRight.draw();
			cam.getTextureReference().unbind();
			eyeFbo.end();
			eyeFbo.readToPixels(eyePixels);
			
			convertColor(eyePixels, gray, CV_RGB2GRAY);
			normalize(gray, gray);
			Sobel(gray, sobelx, CV_32F, 1, 0, 3, 1);
			Sobel(gray, sobely, CV_32F, 0, 1, 3, 1);
			sobel = abs(sobelx) + abs(sobely);
			bitwise_not(gray, gray);
			gray.convertTo(grayFloat, CV_32F);
			sobel += grayFloat;
			
			rowMean = meanRows(sobel);
			// clear the ends
			rowMean.at<float>(0) = 0;
			rowMean.at<float>(rowMean.rows - 1) = 0;
			// build the line
			rowMeanLine.clear();
			float avg = 0, sum = 0;
			for(int i = 0; i < rowMean.rows; i++) {
				float cur = rowMean.at<float>(i);
				avg += i * cur;
				sum += cur;
				rowMeanLine.addVertex(cur, i);
			}
			avg /= sum;
			rowGraph.addSample(avg - runningMean);
			runningMean = 0;//ofLerp(runningMean, avg, .3);
			
			Mat sobelImgMat = toCv(sobelImg);
			imitate(sobelImg, gray);
			sobel.convertTo(sobelImgMat, CV_8U, .5);
			sobelImg.update();
            
            //update the circular buffer
            smoothedBlinks[bufferPos]=rowGraph.getState();
            bufferPos++;
            if(bufferPos>=smoothedBlinks.size()){
                bufferPos=0;
            }
             smoothedBlinkVal = 0;
            for(int i=0;i<smoothedBlinks.size();i++){
                smoothedBlinkVal+=smoothedBlinks[i];
            }
            smoothedBlinkVal/=smoothedBlinks.size();
          //  cout<<smoothedBlinkVal<<endl;
            int blink;
            if(smoothedBlinkVal>=0.5){
                blink = 1;
            }else{
                blink = 0;
            }
            if (PBlink!=rowGraph.getState()) {
               // cout<<"BLINK STATE CHANGE: "<<ofToString(numBlinks)<<endl;
                numBlinks++;
            }
            PBlink=rowGraph.getState();
			//cout<<"blink state "<< rowGraph.getState()<<endl;
            if(rowGraph.getState()==1){
                lampIsOn=true;
            }
            else{
                lampIsOn=false;
            }
			//ofxOscMessage msg;
			//msg.setAddress("/gesture/blink");
			//msg.addIntArg(rowGraph.getState() ? 1 : 0);
			//osc.sendMessage(msg);
		}
	}
    if(lampIsOn&&!timer.getIsLocked()){
        offDuration = timer.get();
      //  cout<<"lamp on"<<endl;
        //find the length of the interval
        
        //character space should be 3* baseUnitOfTime
        if (offDuration>baseUnitOfTime*2.5 && offDuration <baseUnitOfTime *5) {
            //this is a character space
            
            //if the character is a space then kill the word and start a new one
            if (getCharacterFromMorse(currentMorse)=="~") {
                morseSentence="";
                currentMorse="";
                ////cout<<"space"<<endl;
                
            }
            else{
                //break line if it's too long for screen
                int maxNumberOfCharacters = 6;
                /*if (morseSentence.size()<maxNumberOfCharacters) {
                 morseSentence+=getCharacterFromMorse(currentMorse);
                 cout<<"short word"<<endl;
                 }
                 else{
                 morseSentence+="\n"+getCharacterFromMorse(currentMorse);
                 cout<<"long word"<<endl;
                 }*/
                if(morseSentence.size() % maxNumberOfCharacters==0 && morseSentence.size()!=0){
                    morseSentence+="\n-"+getCharacterFromMorse(currentMorse);
                }
                else{
                    morseSentence+=getCharacterFromMorse(currentMorse);
                }
                
                currentMorse="";
            }
            
        }
        else if(offDuration>=baseUnitOfTime*6){
            //this is word space
            currentMorse="";
            
        }
        else if(offDuration<=baseUnitOfTime*2){
            //this is an inter dash gap and can be ignored
        }
        
        timer.setIsLocked(true);
        timer.start();
    }
    //if we've just finished a flash
    if(!lampIsOn && timer.getIsLocked()){
        // cout<<"lamp off"<<endl;
        onDuration = timer.get();
        
        //find the length of the interval  - there is some tolerance built into the system
        if (onDuration>baseUnitOfTime*0.5 && onDuration <baseUnitOfTime *2) {
            //this is a dot
            currentMorse+=".";
        }
        else if (onDuration>=baseUnitOfTime*2 && onDuration <baseUnitOfTime *8) {
            //this is a dash
            currentMorse+="-";
        }
        timer.setIsLocked(false);
        timer.start();
    }

}

void testApp::draw() {
	ofSetColor(255);
	cam.draw(0, 0);
	tracker.draw();
	leftRectImg.draw();
	rightRectImg.draw();
	ofDrawBitmapString(ofToString((int) ofGetFrameRate()), 10, ofGetHeight() - 20);
	ofPushMatrix();
	ofTranslate(10, 10);
	eyeFbo.draw(0, 0);
	
	ofTranslate(0, 48 + 10);
	sobelImg.draw(0, 0);
	
	ofNoFill();
	ofPushMatrix();
	ofTranslate(128, 0);
	ofScale(.3, 1);
	rowMeanLine.draw();
	ofPopMatrix();
	
	ofTranslate(0, 48 + 10);	
	rowGraph.draw(0, 0, 64);
    ofPopMatrix();
    ofSetColor(255);
    ofDrawBitmapString("Average blink: "+ofToString(smoothedBlinkVal), 10, ofGetHeight() - 130);
    ofDrawBitmapString("Current translation: "+morseSentence, 10, ofGetHeight() - 100);
    ofDrawBitmapString("Current message: "+currentMorse, 10, ofGetHeight() - 70);

}
void testApp::mousePressed() {
	
}
void testApp::keyPressed(int key) {
	if(key == 'r') {
		tracker.reset();
	}
    if(key == 'x') {
		currentMorse="";
        translation="";
        morseSentence="";
        cout<<"clear all";
	}
}
void testApp::loadMorseFromFile(){
    string line;
    const char* filePath=ofToDataPath("morse.txt").c_str();
    ifstream myfile (filePath);
    if (myfile.is_open())
    {
        while ( myfile.good() )
        {
            getline (myfile,line);
            
            vector<string> twoHalves = ofSplitString(line, "\t");
            textTranslation.push_back(twoHalves[0]);
            morseCode.push_back(twoHalves[1]);
            //cout << line << endl;
        }
        myfile.close();
    }
    else{
        
        //cout<<"can't open file \n";
    }
}

string testApp::getCharacterFromMorse(string morse){
    
    for(int i=0;i<morseCode.size();i++){
        if(morseCode[i]==morse){
            return textTranslation[i];
        }
    }
    string NoMatch = "!";
    return  NoMatch;
}