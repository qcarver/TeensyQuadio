#include <Audio.h>
#include <Wire.h>

// Audio **************************************************
// Use these with the Teensy Audio Shield
#define SDCARD_CS_PIN    10
#define SDCARD_MOSI_PIN  7
#define SDCARD_SCK_PIN   14

const int sampleSize = sizeof (unsigned short);
const int numBuffers = 4;

//You can WYSIWYG these pjrc objects here: https://www.pjrc.com/teensy/gui/

// hw objects
AudioPlaySdRaw playRaw1;
AudioInputI2SQuad i2sQuadIn;
AudioOutputI2SQuad i2sQuadOut;
AudioControlSGTL5000 audioShield0;
AudioControlSGTL5000 audioShield1;

//queues which will chache audio before saving to files
AudioRecordQueue queue[numBuffers];

//for debugging
AudioAnalyzePeak peak1;
AudioConnection patchCord1(i2sQuadIn, 0, peak1, 0);

//inputs from mics to queues
AudioConnection patchCord2(i2sQuadIn, 0, queue[0], 0);
AudioConnection patchCord3(i2sQuadIn, 1, queue[1], 0);
AudioConnection patchCord4(i2sQuadIn, 2, queue[2], 0);
AudioConnection patchCord5(i2sQuadIn, 3, queue[3], 0);

//outputs to headphones from first audio file
AudioConnection patchCord6(playRaw1, 0, i2sQuadOut, 0);
AudioConnection patchCord7(playRaw1, 0, i2sQuadOut, 1);
AudioConnection patchCord8(playRaw1, 0, i2sQuadOut, 2);
AudioConnection patchCord9(playRaw1, 0, i2sQuadOut, 3);

//</WYSIWYG>

// which input on the audio shield will be used?
const int myInput = AUDIO_INPUT_LINEIN;


// Globals ***********************************************

// Remember which mode we're doing
int mode = 0; // 0=stopped, 1=recording, 2=playing
// The file where data is recorded
File theFile;
File colaFile;
const char THE_FILE_NAME[] = "FILE.RAW";
const char COLLATED_FILE[] = "COLA.RAW";
const int bufferSize = 512; //bytes
const int entireBufferSize = bufferSize * numBuffers;
unsigned long int currentFrame = 0;
byte swapBuffer[bufferSize];
byte buffer[entireBufferSize];
unsigned int samplesAvailable = 0;
unsigned short dirtyBuffer = 0;
long bufferOverruns = 0;

// *******************************************************

/**
  @return void
  @brief Allocates a special pieces of memory and configures the two audio shields
*/
void audioSetup() {
  // Allocate twice as much audio queue memory as given by PJRC in stereo example
  AudioMemory(120);

  // Enable the audio shield, select input, and enable output
  audioShield0.setAddress(LOW);
  audioShield0.enable();
  audioShield0.inputSelect(myInput);
  audioShield0.volume(0.5);
  audioShield1.setAddress(HIGH);
  audioShield1.enable();
  audioShield1.inputSelect(myInput);
  audioShield1.volume(0.5);
}

/**
  @return void
  @brief Set up the SD card where the audio files will be stored.
*/
void sdSetup() {
  // Initialize the SD card
  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);
  if (!(SD.begin(SDCARD_CS_PIN))) {
    // stop here if no SD card, but print a message
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(500);
    }
  }
}

/**
  @return void
  @brief Gets the file and queues ready for recording
*/
void startRecording() {
  Serial.println("startRecording");
  currentFrame = 0;
  bufferOverruns = 0;
  dirtyBuffer = 0;
  memset(buffer, 0x0, entireBufferSize);

  //Delete files from last recording if they still exit
  if (SD.exists(THE_FILE_NAME)) {
    SD.remove(THE_FILE_NAME);
  }

  //Open files for new recording
  theFile = SD.open(THE_FILE_NAME, FILE_WRITE);

  //All ready to go?
  if (theFile) {
    mode = 1;
    for (int i = 0; i < numBuffers; i++) {
      queue[i].begin();
      queue[i].clear();
    }
  } else {
    Serial.println("Couldn't open one of the file to save to");
  }
}

/**
  @return void
  @brief Does all sampling and SD write. These all take time, we are careful to only do one such task pre call.
*/
void continueRecording() {
  for (int channel  = 0 ; channel < numBuffers ; channel ++) {
    if (dirtyBuffer & 0x1 << channel ) continue;
    if (queue[0].available() >= 2) {                                                      //New samples show up in queue's every <5802 mics
      dirtyBuffer |= (0x1 << channel);
      //Serial.print("Channel ");Serial.println(channel);Serial.print("dirty'd buffer to value of ");Serial.println(dirtyBuffer, BIN);
    }
  }
  if (dirtyBuffer & 0xF) {
    for (int channel  = 0 ; channel < numBuffers ; channel ++) {
      if (queue[channel].available() == 30)bufferOverruns++;                              //helps us check how we are doing.. buffer has max 30 slots
      memcpy(buffer + (channel * bufferSize), queue[channel].readBuffer(), 256);
      queue[channel].freeBuffer();
      memcpy(buffer + (channel * bufferSize) + 256, queue[channel].readBuffer(), 256);
    }
    //reset everybody at once ...
    for (int i = 0; i < numBuffers; queue[i++].freeBuffer()); //<-yes, semicolon    //TODO..could also try rotating who frees first?
    //Serial.println("All dirty'd up, writing ... ");
    theFile.write(buffer, entireBufferSize);                                        //this takes 500 mics ..our biggest (non shield) cost
    //Serial.println(currentFrame);
    dirtyBuffer = 0x0;
    currentFrame++;
    //for(int i = 0; i < entireBufferSize; (i%64 == 63)?Serial.println(buffer[i++],HEX):Serial.print(buffer[i++],HEX)); Serial.println();  //for debugging
    return;
  }
  return;
}

/**
  @return void
  @brief writes any last samples out of the queue and closes files and frees buffers
*/
void stopRecording() {
  Serial.println("stoppingRecording");
  for (int i = 0; i < numBuffers; queue[i++].clear()); //<-yes, semicolon
  theFile.write(buffer, entireBufferSize);
  theFile.flush();
  Serial.print("stoppedRecording, Samples: "); Serial.print(currentFrame); Serial.print(", buffer overruns: "); Serial.println(bufferOverruns);
  Serial.print("stored in the file: "); Serial.print(theFile.name()); Serial.print(" "); Serial.print(theFile.size()); Serial.print(" "); Serial.println("bytes");
  theFile.close();
  mode = 0;
  }

/**
  @return void
  @brief
*/
void sendFile() {

  theFile = SD.open(THE_FILE_NAME);
  colaFile = SD.open(COLLATED_FILE, FILE_WRITE);

  if (theFile) {
    //Serial.println("Sending File...");//leave this comment out.. or it will be part of the file :{
    const unsigned long fileSize = theFile.size();

    //write the buffers out to an output buffer
    for (unsigned long currentByte = 0; currentByte < fileSize; currentByte += entireBufferSize) {
      const unsigned long bytesToMoveNow = ((fileSize - currentByte) < entireBufferSize) ? fileSize - currentByte : entireBufferSize;
      memset(buffer, 0x0, entireBufferSize);
      theFile.read(buffer, bytesToMoveNow);
      //Serial.println("Collating:");Serial.println("Whole Buffer before colate is: ");for(int i = 0; i < entireBufferSize; (i%64 == 63)?Serial.println(buffer[i++],HEX):Serial.print(buffer[i++],HEX)); Serial.println();
      collate4(buffer, entireBufferSize);
      //Serial.println("Collating:");Serial.println("Whole Buffer after collate is: ");for(int i = 0; i < entireBufferSize; (i%64 == 63)?Serial.println(buffer[i++],HEX):Serial.print(buffer[i++],HEX)); Serial.println();
      Serial.write(buffer, entireBufferSize);
      colaFile.write(buffer, entireBufferSize);
    }
    Serial.flush();
    Serial.end();
    colaFile.flush();
    colaFile.close();
    theFile.close();
    //Serial.println("done");//leave this comment out.. or it will be part of the file :{

  } else Serial.println("trouble opening files.. aborting");
}

/**
  @return void
  @brief swaps a ushort array of lenght len with the next highest array of length len in the array
*/
void swap(byte * start, unsigned short int len) {
  ////////dest, src, size               //tried XOR'ing 2byte chunks, no advantage (good compiler?)
  memcpy(swapBuffer, start + len, len);
  memcpy(start + len, start, len); //tried memmove here, it's actually SLOWER!(?)
  memcpy(start, swapBuffer, len);
}

/**
  @return void
  @briefAn O(3(ln2)) solution for collating the samples. This takes about 5600 microseconds for a full
  (256 * 2 * 4 bytes) size buffer. This is about 2600 microsends to slow to do real time, so we do it in post
*/
void collate4(byte * addr, unsigned short int len) //can we make this more efficient?
{
  unsigned short int n = len  / 4;
  if (n > sampleSize) {
    byte *endQ1Left = addr + n/2 ;
    byte *endQ1Right = addr + 5 * n/2;

    //swap w/in left half
    swap(endQ1Left, n / 2);
    //swap w/in right half
    swap(endQ1Right, n/ 2);
    //swap on axis of both halves
    swap(addr + n, n);

    collate4(addr, len / 2);
    collate4(addr + len/2, len / 2);
  }
  return;
}

/**
  @return void
  @brief Called first when the microcontroller boots.

  Initializes all hardware subsystems.
*/
void setup() {
  Serial.begin(115200);
  delay(1000);

  audioSetup();
  sdSetup();

  //clear buffer
  memset(buffer, 0x0, entireBufferSize);

  Serial.println("Press 'r'<enter> to record, 's'<enter> to stop, and 't'<enter> to transfer file");
}

/**
  @return void
  @brief Function that is called repeditly after setup.
*/
void loop() {
  if (Serial.available() > 0) {
    int input = Serial.read();
    //Serial.print("Serial input received: "); Serial.println(input);//DEBUG

    if (input == 'r') {
      Serial.println("Record command received");
      if (mode == 0) startRecording();
    }
    if (input == 's') {
      Serial.println("Stop command received");
      if (mode == 1) stopRecording();
    }
    if (input == 't') {
      //have to stop everything first
      if (mode == 1) stopRecording();
      //give user time to close Arduino serial and catch the file via cmd
      Serial.println("sending file in 10 seconds...");
      long wait = millis();
      while ((millis() - wait) < 10000); //<-- yes, semicolon
      sendFile();
    }
    input = 0;
  }
  if (mode == 1) {
    continueRecording();
  }
}
