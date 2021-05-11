
#include "mbed.h"
#include "mbed_events.h"
#include "MQTTNetwork.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"
#include "stm32l475e_iot01_accelero.h"
#include "accelerometer_handler.h"
#include "fig.h"
#include "magic_wand_model_data.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"
#include "mbed_rpc.h"
#include "uLCD_4DGL.h"
#include <math.h>       /* cos */
#define PI 3.14159265
using namespace std::chrono;
// GLOBAL VARIABLES
WiFiInterface *wifi;
InterruptIn btn2(USER_BUTTON);
EventQueue queue(64 * EVENTS_EVENT_SIZE);
EventQueue queue_g(32 * EVENTS_EVENT_SIZE);
EventQueue mqtt_queue;
EventQueue mode_queue;
Thread mqtt_thread(osPriorityHigh);
Thread t_g;
Thread t_s;
Thread t_mode;


int16_t pData[3] = {0};
int idR[32] = {0};
int indexR = 0;;
//InterruptIn btn3(SW3);
volatile int message_num = 0;
volatile int arrivedcount = 0;
volatile bool closed = false;
constexpr int kTensorArenaSize = 60 * 1024;
const char* topic = "Mbed";
int gesture_index,angle = 30;
uint8_t tensor_arena[kTensorArenaSize];


uLCD_4DGL uLCD(D1, D0, D2);//Problem point. connect ulcd
BufferedSerial pc(USBTX, USBRX);
DigitalIn mypin(USER_BUTTON);
DigitalOut myled(LED3);
bool mode_p = false;
bool tilt_m = false;

void Display(int &i);
void tilt(Arguments *in, Reply *out);
void stop(Arguments *in, Reply *out){tilt_m = !tilt_m;}
void gesture_UI(Arguments *in, Reply *out);
int PredictGesture(float* output);
void gesture();
void messageArrived(MQTT::MessageData& md);
void publish_message(MQTT::Client<MQTTNetwork, Countdown>* client);
void close_mqtt();
void WIFI();
void mode();
RPCFunction rpcG(&gesture_UI, "gesture_UI");
RPCFunction rpcT(&tilt, "tilt");
void mode()
{
  mode_p = !mode_p;
}
void Display(int &i)
{
    uLCD.cls();
    uLCD.text_width(2); //4X size text
    uLCD.text_height(2);
    uLCD.locate(4,4);
    uLCD.printf("%d",i);
    uLCD.line(0, 50, 160, 50, 0xFF0000);
    uLCD.line(0, 90, 160, 90, 0xFF0000);
}
void tilt(Arguments *in, Reply *out)
{
    myled = 1;
    char buffer[200], outbuf[256];
    char strings[20];
    out->putData("\n");
    out->putData("tilt mode");
    tilt_m = true;
    mode_p = false;
    while(tilt_m == true)
    {
        BSP_ACCELERO_Init();
        BSP_ACCELERO_AccGetXYZ(pData);
        double deg = (double)pData[2]/sqrt(pData[0]*pData[0]+pData[1]*pData[1]+pData[2]*pData[2]);
        int tmp = acos(deg) * 180 / 3.1415926;
        Display(tmp);
        if(  deg < cos(angle* PI/ 180.0))
        {
          angle = 0;
          mode_p = true;
          ThisThread::sleep_for(1000ms);
          mode_p = false;
          out->putData("larger than threshold\n");
          break;
        }
    }
    out->putData("exit tilt mode\n");
    myled = 0;
}   
void gesture_UI(Arguments *in, Reply *out){
  bool success = true;
  t_g.start(callback(&queue_g, &EventQueue::dispatch_forever));
  queue_g.call(&gesture);
  // Have code here to call another RPC function to wake up specific led or close it.
  char buffer[200], outbuf[256];
  char strings[20];
}


int PredictGesture(float* output) {
    // How many times the most recent gesture has been matched in a row
    static int continuous_count = 0;
    // The result of the last prediction
    static int last_predict = -1;     
    // Find whichever output has a probability > 0.8 (they sum to 1)
    int this_predict = -1;
    for (int i = 0; i < label_num; i++) {
      if (output[i] > 0.8) this_predict = i;
    }     
    // No gesture was detected above the threshold
    if (this_predict == -1) {
      continuous_count = 0;
      last_predict = label_num;
      return label_num;
    }     
    if (last_predict == this_predict) {
      continuous_count += 1;
    } else {
      continuous_count = 0;
    }
    last_predict = this_predict;      
    // If we haven't yet had enough consecutive matches for this gesture,
    // report a negative result
    if (continuous_count < config.consecutiveInferenceThresholds[this_predict]) {
      return label_num;
    }
    // Otherwise, we've seen a positive result, so clear all our variables
    // and report it
    continuous_count = 0;
    last_predict = -1;      
    return this_predict;
}

void gesture() 
{
  tilt_m = false;
  // Whether we should clear the buffer next time we fetch data
  bool should_clear_buffer = false;
  bool got_data = false;
  // The gesture index of the prediction
  int gesture_index;
  // Set up logging.
  static tflite::MicroErrorReporter micro_error_reporter;
  tflite::ErrorReporter* error_reporter = &micro_error_reporter;
  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  const tflite::Model* model = tflite::GetModel(g_magic_wand_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    error_reporter->Report(
        "Model provided is schema version %d not equal "
        "to supported version %d.",
        model->version(), TFLITE_SCHEMA_VERSION);
    
  }
  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  static tflite::MicroOpResolver<6> micro_op_resolver;
  micro_op_resolver.AddBuiltin(
      tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
      tflite::ops::micro::Register_DEPTHWISE_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_MAX_POOL_2D,
                               tflite::ops::micro::Register_MAX_POOL_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_CONV_2D,
                               tflite::ops::micro::Register_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_FULLY_CONNECTED,
                               tflite::ops::micro::Register_FULLY_CONNECTED());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_SOFTMAX,
                               tflite::ops::micro::Register_SOFTMAX());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_RESHAPE,
                               tflite::ops::micro::Register_RESHAPE(), 1);
  // Build an interpreter to run the model with
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
  tflite::MicroInterpreter* interpreter = &static_interpreter;
  // Allocate memory from the tensor_arena for the model's tensors
  interpreter->AllocateTensors();
  // Obtain pointer to the model's input tensor
  TfLiteTensor* model_input = interpreter->input(0);
  if ((model_input->dims->size != 4) || (model_input->dims->data[0] != 1) ||
      (model_input->dims->data[1] != config.seq_length) ||
      (model_input->dims->data[2] != kChannelNumber) ||
      (model_input->type != kTfLiteFloat32)) {
    error_reporter->Report("Bad input tensor parameters in model");
    
  }
  int input_length = model_input->bytes / sizeof(float);
  TfLiteStatus setup_status = SetupAccelerometer(error_reporter);
  if (setup_status != kTfLiteOk) {
    error_reporter->Report("Set up failed\n"); 
  }
  error_reporter->Report("Set up successful...\n");
  while (tilt_m == false) {
    // Attempt to read new data from the accelerometer
    got_data = ReadAccelerometer(error_reporter, model_input->data.f,
                                 input_length, should_clear_buffer);
    // If there was no new data,
    // don't try to clear the buffer again and wait until next time
    if (!got_data) {
      should_clear_buffer = false;
      continue;
    }
    // Run inference, and report any error
    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
      error_reporter->Report("Invoke failed on index: %d\n", begin_index);
      continue;
    }
    // Analyze the results to obtain a prediction
    gesture_index = PredictGesture(interpreter->output(0)->data.f);
    // Clear the buffer next time we read data
    should_clear_buffer = gesture_index < label_num;
    // Produce an output
    if (gesture_index < label_num) {
        angle = (gesture_index+3)*10;
        Display(angle);
        error_reporter->Report(config.output_message[gesture_index]);
        if(!mypin)
        {
          printf("set angle %d\n",angle);
          break;
        }
    }
  }
}


void messageArrived(MQTT::MessageData& md) {
    MQTT::Message &message = md.message;
    char msg[300];
    sprintf(msg, "Message arrived: QoS%d, retained %d, dup %d, packetID %d\r\n", message.qos, message.retained, message.dup, message.id);
    printf(msg);
    ThisThread::sleep_for(1000ms);
    char payload[300];
    sprintf(payload, "Payload %.*s\r\n", message.payloadlen, (char*)message.payload);
    printf(payload);
    ++arrivedcount;
}

void publish_message(MQTT::Client<MQTTNetwork, Countdown>* client) {
    if(mode_p)
    {
      MQTT::Message message;
      char buff[100];
      sprintf(buff, "%d", angle);
      message.qos = MQTT::QOS0;
      message.retained = false;
      message.dup = false;
      message.payload = (void*) buff;
      message.payloadlen = strlen(buff) + 1;
      int rc = client->publish(topic, message);
  
      printf("rc:  %d\r\n", rc);
      printf("Puslish message: %s\r\n", buff);
      mode_p = false;
    }
}

void close_mqtt() {
    closed = true;
}

int main() {
  char buf[256], outbuf[256];
  FILE *devin = fdopen(&pc, "r");
  FILE *devout = fdopen(&pc, "w");

  wifi = WiFiInterface::get_default_instance();
  if (!wifi) {
          printf("ERROR: No WiFiInterface found.\r\n");
  }
  printf("\nConnecting to %s...\r\n", MBED_CONF_APP_WIFI_SSID);
  int ret = wifi->connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);
  if (ret != 0) {
          printf("\nConnection error: %d\r\n", ret);
  }
  NetworkInterface* net = wifi;
  MQTTNetwork mqttNetwork(net);
  MQTT::Client<MQTTNetwork, Countdown> client(mqttNetwork);
  //TODO: revise host to your IP
  const char* host = "192.168.145.190";
  printf("Connecting to TCP network...\r\n");

  SocketAddress sockAddr;
  sockAddr.set_ip_address(host);
  sockAddr.set_port(1883);
  printf("address is %s/%d\r\n", (sockAddr.get_ip_address() ? sockAddr.get_ip_address() : "None"),  (sockAddr.get_port() ? sockAddr.get_port() : 0) ); //check settin
  int rc = mqttNetwork.connect(sockAddr);//(host, 1883);
  if (rc != 0) {
          printf("Connection error.");
  }
  printf("Successfully connected!\r\n");
  MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
  data.MQTTVersion = 3;
  data.clientID.cstring = "Mbed";
  if ((rc = client.connect(data)) != 0){
          printf("Fail to connect MQTT\r\n");
  }
  if (client.subscribe(topic, MQTT::QOS0, messageArrived) != 0){
          printf("Fail to subscribe\r\n");
  }  
  t_mode.start(callback(&mode_queue, &EventQueue::dispatch_forever));
  mqtt_thread.start(callback(&mqtt_queue, &EventQueue::dispatch_forever));
  
  btn2.rise(mode_queue.event(&mode));
  //btn3.rise(&close_mqtt);
  mqtt_queue.call_every(500ms,&publish_message, &client);
  while(1) {
    memset(buf, 0, 256);      // clear buffer
    for(int i=0; ; i++) {
      char recv = fgetc(devin);
      if (recv == '\n') {
        printf("\r\n");
        break;
      }
      buf[i] = fputc(recv, devout);
    }
    //Call the static call method on the RPC class
    RPC::call(buf, outbuf);
    printf("%s\r\n", outbuf);
  }
}