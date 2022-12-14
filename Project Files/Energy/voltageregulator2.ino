/*
 * Program written by Yue Zhu (yue.zhu18@imperial.ac.uk) in July 2020.
 * pin6 is PWM output at 62.5kHz.
 * duty-cycle saturation is set as 2% - 98%
 * Control frequency is set as 1.25kHz. 
*/

#include <Wire.h>
#include <INA219_WE.h>

INA219_WE ina219; // this is the instantiation of the library for the current sensor
float open_loop;
float vpd,vb,vout,vref,iL,dutyref,duty_cycle=0.75,current_mA,va,int_count,pout; // Measurement Variables
unsigned int sensorValue0,sensorValue1,sensorValue2,sensorValue3;  // ADC sample values declaration
float vmin=4.2,vmax=6.0,vdesired=5.1; //internal signals
float Ts=0.0001; //1.25 kHz control frequency. It's better to design the control period as integral multiple of switching period.
float current_limit = 2.0;
boolean Boost_mode = 0;
boolean CL_mode = 0;
int relay = 8;


unsigned int loopTrigger;
unsigned int com_count=0;   // a variables to count the interrupts. Used for program debugging.

void setup() {

  //Basic pin setups
  
  noInterrupts(); //disable all interrupts
  pinMode(13, OUTPUT);  //Pin13 is used to time the loops of the controller
  pinMode(3, INPUT_PULLUP); //Pin3 is the input from the Buck/Boost switch
  pinMode(2, INPUT_PULLUP); // Pin 2 is the input from the CL/OL switch
  analogReference(EXTERNAL); // We are using an external analogue reference for the ADC
  pinMode(relay, OUTPUT);
  digitalWrite(relay, HIGH);
  // TimerA0 initialization for control-loop interrupt.
  
  TCA0.SINGLE.PER = 999; //
  TCA0.SINGLE.CMP1 = 999; //
  TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV16_gc | TCA_SINGLE_ENABLE_bm; //16 prescaler, 1M.
  TCA0.SINGLE.INTCTRL = TCA_SINGLE_CMP1_bm; 

  // TimerB0 initialization for PWM output
  
  pinMode(6, OUTPUT);
  TCB0.CTRLA=TCB_CLKSEL_CLKDIV1_gc | TCB_ENABLE_bm; //62.5kHz
  analogWrite(6,120); 

  interrupts();  //enable interrupts.
  Wire.begin(); // We need this for the i2c comms for the current sensor
  ina219.init(); // this initiates the current sensor
  Wire.setClock(700000); // set the comms speed for i2c
  Serial.begin(9600);
}
void loop(){
if(loopTrigger) { // This loop is triggered, it wont run unless there is an interrupt
    digitalWrite(13, HIGH);// set pin 13. Pin13 shows the time consumed by each control cycle. It's used for debugging.
    int_count++;
    sampling();
    digitalWrite(13, LOW); // reset pin13.
    loopTrigger = 0;
  }
     
  if(int_count==1000){
    String datavalue=String(pout) +","+ String(current_mA) +"," + String(vb)+","+String(va)+","+String(duty_cycle);
    Serial.println(datavalue);
    int_count=0;
  }
  
  pout=vb*current_mA;
  vout=vb;
  if(vout<vmin){
    duty_cycle=duty_cycle+0.05; 
  }
  else if(vout<vmax){
      if(vout<vdesired){
        duty_cycle=duty_cycle+0.01;
      }
      else{
       duty_cycle=duty_cycle-0.01;
      }
  }
  else{
    duty_cycle=duty_cycle-0.05;
  }

  duty_cycle=saturation(duty_cycle,0.99,0.01);
  pwm_modulate(duty_cycle);
  

  if((vout<vmin)||(vout>vmax)||(iL>current_limit)){
    digitalWrite(relay,HIGH);
  }
  else{
      digitalWrite(relay,LOW);
  } 

}

// Timer A CMP1 interrupt. Every 800us the program enters this interrupt. 
// This, clears the incoming interrupt flag and triggers the main loop.

ISR(TCA0_CMP1_vect){
  loopTrigger = 1;
  TCA0.SINGLE.INTFLAGS |= TCA_SINGLE_CMP1_bm; //clear interrupt flag  
}

// This subroutine processes all of the analogue samples, creating the required values for the main loop



void sampling(){

  // Make the initial sampling operations for the circuit measurements
  
  sensorValue0 = analogRead(A0); //sample Vb
  sensorValue2 = analogRead(A2); //sample Vref
  sensorValue3 = analogRead(A3); //sample Vpd
  current_mA = ina219.getCurrent_mA(); // sample the inductor current (via the sensor chip)

  // Process the values so they are a bit more usable/readable
  // The analogRead process gives a value between 0 and 1023 
  // representing a voltage between 0 and the analogue reference which is 4.096V
  
  vb = sensorValue0 * (4.096 / 1023.0)*(130.0/20.0); // Convert the Vb sensor reading to volts
  vref = sensorValue2 * (4.096 / 1023.0); // Convert the Vref sensor reading to volts
  vpd = sensorValue3 * (4.096 / 1023.0); // Convert the Vpd sensor reading to volts
  va=(vpd*890)/330;

  // The inductor current is in mA from the sensor so we need to convert to amps.
  // We want to treat it as an input current in the Boost, so its also inverted
  // For open loop control the duty cycle reference is calculated from the sensor
  // differently from the Vref, this time scaled between zero and 1.
  // The boost duty cycle needs to be saturated with a 0.33 minimum to prevent high output voltages
  
 
    iL = current_mA/1000.0;
    dutyref = sensorValue2 * (1.0 / 1023.0);
  
}

float saturation( float sat_input, float uplim, float lowlim){ // Saturatio function
  if (sat_input > uplim) sat_input=uplim;
  else if (sat_input < lowlim ) sat_input=lowlim;
  else;
  return sat_input;
}

void pwm_modulate(float pwm_input){ // PWM function
  analogWrite(6,(int)(255-pwm_input*255)); 
}

// This is a PID controller for the voltage
