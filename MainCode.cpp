#include "I2Cdev.h"
#include "MPU6050.h"
#include "Wire.h"
#include <SFE_BMP180.h>
#include <Servo.h>
#include <SPI.h>
#include <SD.h>


#define R 1716  //(ft*lb)/(slug*degree_R) 
#define g 32.174  //ft/s^2
#define epsilon 0.005 //tolerance for RCS
#define dataSize 101 

//Servo Values
int pos=0,verticalDetect=0;
int startPos=0,setupDetect=0;

//variables 

int 	PoweredTime, OldaccX, BT = 0, H1, H2, H3, launchDetect = 0, Brake3 = 0, Brake2 = 0, Brake1 = 0; 
double  Xprev = 0, PnextAx = 0, PnextALT = 0, alt_prev = 0, alt_refine, Ax;
double  VelocityNew, Velocity, OldVelocity = 0, PositionNew, Position = 0,ALTREFINE;
double  slopeVelocity = 0;
double rho = 0, q = 0;

int degreesPerSecond[dataSize];
int servoAngles[dataSize];


unsigned long T=0,TimeOfLaunch=0,TimeSinceLaunch=0;
unsigned long time=0;    // this is a variable
unsigned long OldTime=0; // this is a variable


//Objects
SFE_BMP180 bmp;
MPU6050 mpu;
Servo servo;
Servo RCservo;
File dataFile, RCdata;

double baseline; // baseline pressure

//MPU 6050 Accelerations
int16_t accX, accY, accZ;

//MPU 6050 gyro measurements
int16_t wX, wY, wZ;


//BMP180 Altitude
double altitude;




/*
Format functions

1. getAcc();
2. getAlt();
3. resetBmp();
4. closeServo(); -- Might have to change
5. openServo();  -- Might have to change

  running these functions updates altitude,accX,accY,accZ;

*/

void setup() {
  //Serial.println("Init!");
  mpuSetup();
  bmpSetup();
  serialSetup();
  servoSetup();
  SDcardSetup(); //set up sd card to read RC data
  RCsetup();
  SDcardWriteSetup(); //setup sd card to write data to
  
}

void loop() { // run code ( main code)
// and so we begin
//if(startPos==0){
//closeServo();
//startPos=1;
//

if (setupDetect==0){
openServo();
	/*do{
		getAcc();
		if (accX>30){
		setupDetect=1;
		closeServo();
		delay(600000);// to allow time for placing on AGSE (10 min delay)
		}
	}while(setupDetect==0);*/ //AGSE stuff?
}	

if(verticalDetect==0){
verticalDetect=VerticalDetect(verticalDetect);
delay(300000);// to be used for launch days only (5 min delay after vertical)
}

// Launch detection have the getAcc built in 
if(launchDetect==0){// this is probably redundant in the function, we could take the function one  //JK
(TimeOfLaunch)=LaunchDetection(&launchDetect);
}

//Serial.print("Time of Launch:");
//Serial.println(TimeOfLaunch);
T=millis(); // this is needed to distinguish from time in brakingloop
TimeSinceLaunch=T-TimeOfLaunch;  
//delay(4000);////////////////////////////////////////////////////////
//Serial.print("Time in ms since Launch:");
//Serial.println(TimeSinceLaunch);
//Serial.print("Current Arduino Time:");
//Serial.println(T);
PoweredTime=2300; 						//<-------------------------------------------------------------------------------------------------Probably change this number

BrakingLoop(TimeSinceLaunch,PoweredTime,&BT);


time=millis();  // current time for measurements
getAcc(); 		// Updates accX,accY,accZ
getAlt(); 		// Updates altitude


//Serial.print("Starting P value:");
//Serial.println(PnextAx);
(Ax)=kalman(accX,    Xprev,   &PnextAx); 
//Serial.print("Ax:");
//Serial.println(Ax);

//Serial.print("Starting PALT value:");
//Serial.println(PnextALT);

(alt_refine)=kalman(altitude,alt_prev, &PnextALT); 
	
//Serial.print("Altrefine:");
//Serial.println(alt_refine);

//Slope Velocity from BMP
slopeVelocity=SlopeVelocity(OldTime,time,alt_prev,alt_refine);
//Serial.print("SlopeVelocity:");
//Serial.println(slopeVelocity);

// integration to for Velocity
//Serial.println(Xprev);
//Serial.println(accX);
//Serial.println(OldTime);
//Serial.println(time);

VelocityNew=integrate(OldTime,time,Xprev,Ax);
OldVelocity=Velocity;
Velocity=Velocity+VelocityNew;

// integration  for position
PositionNew=integrate(OldTime,time,OldVelocity,Velocity);
Position=Position+PositionNew;  // define this as 0 to start




// reassign the acceleration values to oldACCx,oldACCz,oldACCy, time too
OldaccX=accX ;			//define this as  0
OldTime=time;//ms		// this will start at 0 in def
Xprev=Ax;				// Xprev will be started at 0   
alt_prev=alt_refine;  	//alt_prev will started at 0 in definitions




// look at averaging the values for altitude ( for the testing on March 8th we will use the BMP for testing)
ALTREFINE=alt_refine;

H1=1500;   //these are the heights that we will start the braking //Probably change these<-------------------------------------------------------------
H2=2400;
H3=2750;
//Serial.println(ALTREFINE);
//slopeVelocity=300;//////////////////////////////////////////////////
if (TimeSinceLaunch>PoweredTime){
BT=ApogeeCall(ALTREFINE,H1,H2,H3,slopeVelocity,&Brake1,&Brake2,&Brake3);
RollControl();
} //<-----------------------------------------------------------------------------------------Delete this?
//Serial.print("BT: ");
//Serial.println(BT);

FreefallDetection(accX,accY,Brake3);// Freefall
 
Abort(launchDetect);//Abortion

writeData();

}

// Beginning of the Functions

//SD Card

void SDcardSetup(){
  
	pinMode(4, OUTPUT);
  	SD.begin(4);  
}

void SDcardWriteSetup()
{
	dataFile = SD.open("Data.txt", FILE_WRITE);
	dataFile.println("Time(ms),Height(ft),F Alt(ft),AccX(ft/s^2),AccY(ft/s^2),AccZ(ft/s^2),F Acc(ft/s^2),SlopeVel(ft/s)");
	dataFile.close();	
}

void writeData(){
 dataFile = SD.open("Data.txt", FILE_WRITE);
 dataFile.print(millis());
 dataFile.print(",");
 dataFile.print(altitude);
 dataFile.print(",");
 dataFile.print(alt_refine);
 dataFile.print(",");
 dataFile.print(accX);
 dataFile.print(",");
 dataFile.print(accY);
 dataFile.print(",");
 dataFile.print(accZ);
 dataFile.print(",");
 dataFile.print(Ax);
 dataFile.print(",");
 dataFile.println(slopeVelocity);
 dataFile.close();
}

void abortWrite(){
dataFile = SD.open("Data.txt", FILE_WRITE);
dataFile.println("ABORT DETECTED");
dataFile.close();
}

void launchDetectWrite(){
dataFile = SD.open("Data.txt", FILE_WRITE);
dataFile.println("LAUNCH DETECTED");
dataFile.close();
}

void freefallWrite(){
dataFile = SD.open("Data.txt", FILE_WRITE);
dataFile.println("FREEFALL DETECTED");
dataFile.close();
}

void verticalWrite(){
dataFile = SD.open("Data.txt", FILE_WRITE);
dataFile.println("VERTICAL DETECTED");
dataFile.close();
}

void brake1Write(){
dataFile = SD.open("Data.txt", FILE_WRITE);
dataFile.print("BRAKE1 DETECTED TIME:");
dataFile.println(millis());
dataFile.close();
}

void brake2Write(){
dataFile = SD.open("Data.txt", FILE_WRITE);
dataFile.print("BRAKE2 DETECTED TIME:");
dataFile.println(millis());
dataFile.close();
}

void brake3Write(){
dataFile = SD.open("Data.txt", FILE_WRITE);
dataFile.print("BRAKE3 DETECTED TIME:");
dataFile.println(millis());
dataFile.close();
}

void openWrite(){
dataFile = SD.open("Data.txt", FILE_WRITE);
dataFile.println("BRAKE OPENED");
dataFile.close();
}

void closeWrite(){
dataFile = SD.open("Data.txt", FILE_WRITE);
dataFile.println("BRAKE CLOSED");
dataFile.close();
}

void btWrite(){
dataFile = SD.open("Data.txt", FILE_WRITE);
dataFile.print("BT:");
dataFile.println(BT);
dataFile.close();
}

void rcWrite()
{
	dataFile = SD.open("data.txt", FILE_WRITE);
	dataFile.print("RC:");
}


//Serial Setup
void serialSetup(){
  
 Serial.begin(9600);
 
}

//Servo Methods                                   Adjust this to the current settings
void servoSetup(){
  servo.attach(9);
  servo.write(0);
  RCservo.attach(10);
  RCservo.write(0);
}

void closeServo(){
	if (pos>0){
  do{
	//Serial.println(pos);
	pos -= 10;
	//140degree                      // in steps of 5 degree 
	servo.write(pos);              // tell servo to go to position in variable 'pos' 
	delay(20);    					// waits 15ms for the servo to reach the position 
	}while(pos >0);
	}
	/*for(pos; pos <= 0; pos--){
		servo.write(pos);
		delay(1);
	}*/
	}

void openServo(){
if (pos<200){
 do{
	//Serial.println(pos);
	pos += 10;
	//140degree                      // in steps of 5 degree 
	servo.write(pos);              // tell servo to go to position in variable 'pos' 
	delay(20);    					// waits 15ms for the servo to reach the position 
	}while(pos <=200);
} 
	/*for(pos; pos >= 200; pos++){
		servo.write(pos);
		delay(1);
	}
	}*/
}

//BMP 180 Methods
void bmpSetup(){
  
  bmp.begin();
  baseline = getPressure();
}

double getPressure(){
  char status;
  double T,P,p0,a;

  // You must first get a temperature measurement to perform a pressure reading.
  
  // Start a temperature measurement:
  // If request is successful, the number of ms to wait is returned.
  // If request is unsuccessful, 0 is returned.

  status = bmp.startTemperature();
  if (status != 0)
  {
    // Wait for the measurement to complete:

    delay(status);

    // Retrieve the completed temperature measurement:
    // Note that the measurement is stored in the variable T.
    // Use '&T' to provide the address of T to the function.
    // Function returns 1 if successful, 0 if failure.

    status = bmp.getTemperature(T);
    if (status != 0)
    {
      // Start a pressure measurement:
      // The parameter is the oversampling setting, from 0 to 3 (highest res, longest wait).
      // If request is successful, the number of ms to wait is returned.
      // If request is unsuccessful, 0 is returned.

      status = bmp.startPressure(3);
      if (status != 0)
      {
        // Wait for the measurement to complete:
        delay(status);

        // Retrieve the completed pressure measurement:
        // Note that the measurement is stored in the variable P.
        // Use '&P' to provide the address of P.
        // Note also that the function requires the previous temperature measurement (T).
        // (If temperature is stable, you can do one temperature measurement for a number of pressure measurements.)
        // Function returns 1 if successful, 0 if failure.

        status = bmp.getPressure(P,T);
        if (status != 0)
        {
          return(P);
        }
        else Serial.println("error retrieving pressure measurement\n");
      }
      else Serial.println("error starting pressure measurement\n");
    }
    else Serial.println("error retrieving temperature measurement\n");
  }
  else Serial.println("error starting temperature measurement\n");
}


double getAltitude(){
  double a,P;
  
  // Get a new pressure reading:

  P = getPressure();

  // Show the relative altitude difference between
  // the new reading and the baseline reading:
  //---------------
  a = bmp.altitude(P,baseline);
  
  return (a*3.28084);
  
}

void getAlt(){
  altitude = getAltitude();
}

void resetBmp(){
  baseline = getPressure();
}

// MPU 0650 Methods
void getAcc(){
   mpu.getAcceleration(&accX, &accY, &accZ);
   accX = map(accX, 0, 4096, 0, 32);
   accY = map(accY, 0, 4096, 0, 32);
   accZ = map(accZ, 0, 4096, 0, 32);
}

void mpuSetup(){
     #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
        Wire.begin();
    #elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
        Fastwire::setup(400, true);
    #endif
    mpu.initialize();
    mpu.setFullScaleAccelRange(2);  //0=2g, 1=4g, 2=8g, 3=16g
    mpu.setFullScaleGyroRange(2); //0=250 deg/s, 1=500 deg/s, 2=1000 deg/s, 3=2000 deg/s 
    calibrateMPU();
}

double kalman(double UnFV,double FR1,double *Pold){

/* function filtering results in real time with Kalman filter */
//Serial.println("Kalnam function initialized...");
//Serial.println(UnFV,FR1,*Pold);
	int A=1,un=0,H=1,B=0;
	double Prediction,P,y,S,K,FR2;

	double Q,R; 
		Q=0.1;
		R=0.2;      // defined these from the Matlab code
	// inputs are Unfiltered Response, Filtered  response, Prediction value old
	// Q and R are sensor specific values (probably)
	// make sure to predefine P and possibly other values at the beginning of the main code. starts at 1

	//UnFV		    	// unfiltered value from sensors read in (zn)						-internal logic
	//FR1               // read this variable in from main (AX(i)) defined last iteration	-internal logic
	//Pold    			// output Pold and read in Pold from last iteration					-input,output
	//Q					// sensor specific value, we might default this						-input
	//R					// sensor specific value, we might default this						-input
	//A					
	
	Prediction= A*FR1 +B*un; // State Prediction 		(Predict where we're goning to be)
	P=A*(*Pold)*A +Q;		 // Covariance Prediction 	(Predict how much error)
	y=UnFV-H*Prediction;	 // Innovation  			(Compare reality against prediction)
	S= H*P*H +R;			 // innovation Covariance 	(Compare real error against prediction)
	K=(P*H)/S;				 //Kalman Gain 				(Moderate the prediction)
	FR2=Prediction+K*y;		 //state update 			(New estimate of where we are)
	(*Pold)=(1-K*H)*P;		 //Covariance update 		(New estimate of error)
	
	return FR2;
	
 
    
}

double ApogeeCall(double ALTREFINE,int H1, int H2, int H3, double V,int *Brake1, int *Brake2,int *Brake3){
	double BT;
	int HT;
	//Serial.println("Into Apogee Call");
	//this code has a prediction built in  to it
	
	// we want to make a code that will still deploy air brakes if we accidentally exceed an altitude
	// without deploying the brakes
	
	// ALTREFINE  is the mediated result from the BMP and MPU					-input
	// H1, H2 ,H3 are altitudes that we want to check what's going on           -input 
	// V, Current velocity														-input
	// BT is the time brakes will be deployed	-output			
	//ALTREFINE=2001;/////////////////////////////////////////////////////////////////////////////////
	// BrakeN is a counter to ensure only one braking cycle per section.		-internal logic
if ( (*Brake1==0) && (ALTREFINE >= H1)){
	// HT is the target height for the iteration
	HT=3070; // ft
	BT=ApogeePrediction(ALTREFINE,V,HT);
	
	// What if the Prediction is lower than HT?
	*Brake1=1;
	//Serial.println("BRAKE1-----------------");
	brake1Write();
}

	else if ((ALTREFINE>=H2) && (*Brake2==0)){
		HT=3025; // ft
	BT=ApogeePrediction(ALTREFINE,V,HT);
	// include in this function the turn on key
	*Brake2=1;
	brake2Write();
}
	else if ((ALTREFINE>=H3) && (*Brake3==0)){
		HT=3000; // ft
	BT=ApogeePrediction(ALTREFINE,V,HT);

	// include in this function the turn on 
	*Brake3=1;
	brake3Write();
}
	else {
	BT=0;
	}

return BT;
}

double ApogeePrediction( double ALTREFINE, double V, double HT) {
	double NRG1,NRG2,Vout,ACC,FrontArea,D,BT;
	int MassE,G;
	//Serial.println(	"into Apogee Prediction");
	//G 		    	// Value of gravity in ft/s^2			-internal logic
	//Hn                // Current Height value					-input
	//HT				// Height of Target apogee				-input
	//MassE				// Mass of rocket Empty					-internal logic
	//NRG1				// Current Energy of rocket				-internal logic
	//NRG2				// Desired Energy of rocket				-internal logic
	//V					// Velocity in ft/s						-input
	//Vout				// Velocity we need to be at currently	-internal logic
	//ACC				// Acceleration due to drag and Gravity	-internal logic
	//BT				// Braking time(s)						-output
	//D					// Drag Force							-internal logic
	//FrontArea			// Frontal area of rocket				-internal logic
	//BT 				// Braking Time ms						-output
	
	// internal logic variable declarations
	MassE=50; // True mass is 26.5 some   lb //Change this<----------------------------------------------------------------------------------
	G=32; // ft/s
	
	//drag equation
	FrontArea=.78; //ft^2
	D=.85*((0.0023769*V*V)/2)*FrontArea;
	ACC=D/MassE;
	//Serial.print("D:");
	//Serial.println(D);
	//Serial.print("ACC:");
	//Serial.println(ACC);
	
	
	
	
	// double check the drag equation here. cause its just kinda arbitrary
	NRG1= MassE*G*ALTREFINE;
	NRG2= MassE*G*HT;
	Vout= sqrt(((NRG2-NRG1)*2)/MassE);         // might need                   DOUBLE CHECK THE MATH HERE
	//Serial.print("Vout:");
	//Serial.println(Vout);
	BT=(V-Vout)/(ACC+32);      // this is for drag     // BT should be in seconds
	BT=BT*1000; // convert to milliseconds
	
	//BT=3000;///////////////////////////////////////////////////////////////
	if (BT>0){
	openServo(); // this will open the servo for this iteration if necessary this should be the only way to open the brakes
	//Serial.println("OPEN BRAKES");
	openWrite();
	}
	//Serial.print("BT:::::");
	//Serial.println(BT);
   return BT;
}

double integrate(unsigned long time1, unsigned long time2, double Val1, double Val2) {

	//function requires two times, and two  data points

	double Time2, Time1;
	Time2=time2/1000;
	Time1=time1/1000;

	//Area						//result of integration  	-output
	//Val1						//input of initial value	-input
	//Val2						//input of secondary inital -input
	//deltaT					//change in time			-internal logic
	//deltaA					//change in value			-internal logic
	//time2						//time previous				-input
	//time1						//time current				-input


	double deltaT, deltaA;     // declares Change in time, Change in Acceleration
	double Area; 			   // declares Final Area
	deltaT = Time2-Time1;      // computes deltaT
	deltaA = (Val2+Val1)/2;    // computes deltaA
	Area = deltaT*deltaA; 	   // computes Area
	
	return Area; 
}	

void BrakingLoop(unsigned long TimeSinceLaunch, int PoweredTime, int *BT){     // double check for class definition and output class

	//TimeSinceLaunch			// Time since launch detection					- input 
	//PoweredTime 		    	// The time that the motor will burn (ms)		- input (also for internal logic
	//BT						// The braking time duration that  (ms)			- input	/ output with pointer		
	//TimeStep					// time since Breaking started					-internal logic
	int TimeStep,TimeStepStart;
	//Serial.println("Into Brakingloop");
	//Serial.print("Starting loop at:");
	//Serial.println(millis());
	if (TimeSinceLaunch>PoweredTime){
		//Serial.println("Powered time passed");
		if (*BT>0){
		//Serial.println("BT is greater than 0");
		btWrite();
		TimeStepStart=millis();
			do{
			//Serial.println(" Braking...");
			TimeStep=millis()-TimeStepStart;
			getAcc();
			getAlt();
			writeData();
					if (TimeStep>=*BT){
					closeServo(); 		// Closing the brakes state   will call on function  this will be the only closing function
					//Serial.println("Closing servo");
					//Serial.print("At Time:");
					
					//Serial.println(millis());
					TimeStep=0;  		// Resetting the counter
					*BT=0    ;			// Reset the Braking time to 0 (this is a back up procedure)
					}
			}while(*BT>0);
		}

	}

}

void FreefallDetection(int accX,int accY,int Brake3){
int freefallDetect;
	if ((accX <= 1 && accX >= -50)&&(Brake3==1)){ 
	//------------------------------------------------ try dropping that shit
		freefallDetect=1;
		freefallWrite();
		closeServo();
		do {
			//Serial.println("FREEFALL");//Check to see how this will react to conditions
			getAcc();
			getAlt();
			delay(200);
			writeData();
		}while(freefallDetect==1);
	}
}

unsigned long LaunchDetection(int *launchDetect){
 
 //TimeOfLaunch 			//The time on global clock when launch is detected		-output
 //launchDetect				// conditional for operating 
 
 unsigned long TimeOfLaunch;
	//Serial.println("Launch Detection Initialized");
	//Serial.println(*launchDetect);
	if (*launchDetect==0){
		//Serial.println("No launch Detected");// for testing
		do{
			getAcc();
			if (accX>=45){// figure out the acceleration right after launch----------------------------------------------------------
					*launchDetect=1;
					//Serial.println(accX);
					TimeOfLaunch=millis();
					//Serial.println("Launch detected----------------------------------");// for testing
					launchDetectWrite();
					//Serial.println(TimeOfLaunch);
					//Serial.println(*launchDetect);
					
			}
		}while(*launchDetect==0);
		
	return TimeOfLaunch;
	}
	 
}
		
void Abort(int launchDetect){
 
 int abortDetect=0;
		getAcc();
		//Serial.println(accX);
		//Serial.println(accY);
		//Serial.println(accZ);
	if ((launchDetect==1) && ( (abs(accY)>=32 ) || (abs(accZ)>=32))){
		//Serial.print("Aborting mission");
		abortWrite();
		abortDetect=1;
		closeServo();
		do{
			getAcc();
			getAlt();
			//Serial.println("Writing...");
			delay(200);
			writeData();
			}while(abortDetect==1);
	}
			
}

int VerticalDetect( int verticalDetect){
// to detect if vertical has happened

	if(verticalDetect==0){
		do{
		
		//Serial.println("Not vertical");
		getAcc();
			if(accX>=25){
			verticalDetect=1;
			verticalWrite();
			//Serial.print("Vertical Detected");
			}
			
		}while(verticalDetect==0);
	}
	return verticalDetect;
}
	
double SlopeVelocity(unsigned long OldTime, unsigned long time, double alt_prev, double alt_refine){
	double Slope,OldTimeDouble,timeDouble;
	OldTimeDouble=OldTime;
	timeDouble=time;
	//Serial.print("alt_refine:");
	//Serial.println(alt_refine);
	//Serial.print("alt_prev:");
	//Serial.println(alt_prev);
	//Serial.print("OldTime:");
	//Serial.println(OldTimeDouble);
	//Serial.print("time:");
	//Serial.println(timeDouble);
	Slope=((alt_refine-alt_prev)/((timeDouble/1000)-(OldTimeDouble/1000)));
	return Slope;
}
	


//--------------------------------------------------------------------------------------------------------Roll Control Shit--------------------------------------------------------------------------------------------------------

//Recalibrate before the actual launch
//Just run the MPU_6050.ino file and enter the numbers here
void calibrateMPU()
{
	mpu.setXAccelOffset(368);
    mpu.setYAccelOffset(-2364);
    mpu.setZAccelOffset(1490);
    mpu.setXGyroOffset(-11);
    mpu.setYGyroOffset(-46);
    mpu.setZGyroOffset(51);
}

void updateGyro()
{
	updateAccel();
	updateOmega();
}

void updateAccel()
{
	//change last 2 values to 64, 128, 256, or 512 depending on acceleration range
	mpu.getAcceleration(&accX, &accY, &accZ);
	accX = map(accX, -32768, 32767, -512, 512);
	accY = map(accY, -32768, 32767, -512, 512);
	accZ = map(accZ, -32768, 32767, -512, 512);
}

void updateOmega()
{
	//change last 2 values to 250, 500, 1000, or 2000 depending on angular velocity range
	mpu.getRotation(&wX, &wY, &wZ);
	wX = map(wX, -32768, 32767, -1000, 1000);
	wY = map(accY, -32768, 32767, -1000, 1000);
	wZ = map(accZ, -32768, 32767, -1000, 1000);
}

//integrates acceleration to find and return current velocity
double getVelocity()
{
	OldVelocity = velocity;
	updateAccel();
	(Ax)=kalman(accX,    Xprev,   &PnextAx); 
	velocity += integrate(OldTime, time, Xprev, Ax);
	OldaccX = accX;
	OldTime = time;
	Xprev = Ax;
}


//uses pressure and temperature from BMP to calculate air density
double getAirDensity()
{
	double p = getPressure();
	double t;
	char status;
	status = bmp.startTemperature();
	if(status != 0)
	{
		delay(status);
		bmp.getTemperature(t);
	}

	rho = p / (R * t);
	return rho;
}

double getDynamicPressure()
{
	getVelocity();
	q = 0.5 * getAirDensity() * Math.pow(velocity, 2); //q = 0.5*rho*v^2
}


void RCsetup()
{
	RCdata = SD.open("RCdata.txt", FILE_WRITE);
	for(int i = 0; i < dataSize - 1; i++)
	{
		// degreesPerSecond[i] = RCdata.read();
		Serial.print("Degrees per second at "); Serial.print(i); Serial.print(": ")
		Serial.println(RCdata.read());
	}
	for(int i = 0; i < dataSize - 1; i++)
	{
		// servoAngles[i] = RCdata.read();
		Serial.print("Servo Angles at "); Serial.print(i); Serial.print(": ")
		Serial.println(RCdata.read());
	}
}

//Do this better with recursion dip shit
void rollControl()
{
	int time = millis();
	//if not currently breaking
	if(servo.read() == 0 && BT == 0)
	{
		updateOmega();
		bool success = false;
		int index = 50;
		int size = dataSize - 1;
		int start = 0;
		int end = size;
		while(!success)
		{
			double diff = wX - degreesPerSecond[index];
			if(Math.abs(dif) < epsilon)
			{
				RCservo.write(servoAngles[index]);
				success = true;
			}
			else if(diff > 0)
			{
				start = index;
			}
			else
			{
				end = index;
			}

			index = (start + end) / 2;
			if(index >= size)
			{
				RC.servo.write(servoAngles[size]);
				success = true;
			}
			else if(index <= 0)
			{
				RCservo.write(servoAngles[0]);
				success = true;
			}
		}
	}
	time = millis() - time;
	Serial.println(time);
}



	

		