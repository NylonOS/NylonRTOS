
/************************************************************************
*
* FILENAME :        main.c
*
* DESCRIPTION :
*       The main program file.
*
* PUBLIC FUNCTIONS :
*       char *getOutletId()
*       int getDevInfo(HttpdConnData *);
*		int getSiteSurvey(HttpdConnData *);
*		int setSrvInfo(HttpdConnData *);
*		int setConnection(HttpdConnData *);
*		cJSON *get_upload(uint8);
*
*
* NOTES :
*       These functions are a part of the FM suite;
*       See IMS FM0121 for detailed description.
*
*       Copyright TISC, LTD. 2013.  All rights reserved.
*
* AUTHOR :    Jeff       START DATE :    2018/11/27
*
****************************************************************************/
#include <string.h>
#include <espressif/esp_common.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <esp/uart.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <dhcpserver.h>
#include <lwip/dns.h>
#include <stdio.h>

#include <unistd.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include <lwip/sys.h>
#include <string.h>


#include <stdlib.h>
#include <stdbool.h>
#include <libesphttpd/httpd.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <cJSON.h>
#include <paho_mqtt_c/MQTTESP8266.h>
#include <paho_mqtt_c/MQTTClient.h>
#include <semphr.h>
#include <sysparam.h>
#include <http_client_ota.h>
/*******************************************************************************/
#include "tiscservice.h"

#define vTaskDelayMs(ms) vTaskDelay((ms) / portTICK_PERIOD_MS)


char *rssiInfo = NULL;




HttpdBuiltInUrl builtInUrls[]={
			{"/cgi-bin/api/SetSrvInfo",setSrvInfo , NULL},
			{"/cgi-bin/api/GetSiteSurvey", getSiteSurvey, NULL},
			{"/cgi-bin/api/SetConnection", setConnection, NULL},
			{"/cgi-bin/api/GetDevInfo", getDevInfo, NULL},
			//{"/cgi-bin/tisc/SetSrvInfo",setSrvInfo , NULL},
			//{"/cgi-bin/tisc/GetSiteSurvey", getSiteSurvey, NULL},
			//{"/cgi-bin/tisc/SetConnection", setConnection, NULL},
			//{"/cgi-bin/tisc/GetDevInfo", getDevInfo, NULL},
			{NULL, NULL, NULL}
		};








uint8_t cmd[COMMAND_SIZE+1]={
							0x60,
							0x01,
							0x02,
							0x03,
							0x04,
							0x05,
							0x06,
							0x07,
							0x09,
							0x10,
							0x11,
							0x12,
							0x13,
							0x14,
							0x21,
							0x4C,
							0x4F,
							0x50,
							0x51,
							0x52,
							0x53,
							0x54,
							0x55,
							0x56,
							0x57,
							0x58,
							0x59,
							0x5A,
							0x5B,
							0x5E,
							0x61,
							0x62,
							0x63,
							0x64,
							0x65,
							0x67,
							0x68,
						};






Set mapping[]=	{
					{"81",0x01,NULL},
					{"82",0x02,NULL},
					{"83",0x03,NULL},
					{"84",0x04,NULL},
					{"85",0x05,NULL},
					{"86",0x06,NULL},
					{"87",0x07,NULL},
					{"90",0x10,NULL},
					{"91",0x11,NULL},
					{"92",0x12,NULL},
					{"93",0x13,NULL},
					{"94",0x14,NULL},
					{"B0",0x30,NULL},
					{"B1",0x31,NULL},
					{"B2",0x32,NULL},
					{"B3",0x33,NULL},
					{"E1",0x61,NULL},
					{"E2",0x62,NULL},
					{"E3",0x63,NULL},
					{"E4",0x64,NULL},
					{"CC",0x4C,NULL},
					{"CF",0x4F,NULL},
					{"E5",0x65,NULL},
					{NULL,NULL,NULL}
				};








SemaphoreHandle_t wifi_alive;
SemaphoreHandle_t write_flag;

QueueHandle_t publish_queue;
QueueHandle_t write_queue;

QueueHandle_t restart_queue;


sdk_os_timer_t beat_timer;
sdk_os_timer_t heap_timer;

sdk_os_timer_t reset_timer;


Device *device = NULL;

/*Dictionary *sub_status[16]={NULL};*/

DeviceData device_data[16] = {NULL};


uint32 sub_list = 0x00000001;


uint8 device_address = 0x01;

uint32 version = 0;

uint8 reset_count = 0;

uint8 blink_mode = 0;


static char path1[17] = "\0";
static char path2[20] = "\0";

uint8_t mqtt_buf[2048];
uint8_t mqtt_readbuf[512];


static int  host2addr(const char *hostname , struct in_addr *in)
{
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_in *h;
    int rv;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    rv = getaddrinfo(hostname, 0 , &hints , &servinfo);
    if (rv != 0)
    {
        return rv;
    }

    // loop through all the results and get the first resolve
    for (p = servinfo; p != 0; p = p->ai_next)
    {
        h = (struct sockaddr_in *)p->ai_addr;
        in->s_addr = h->sin_addr.s_addr;
    }
    freeaddrinfo(servinfo); // all done with this structure
    return 0;
}


static inline void ota_error_handling(OTA_err err) {
    printf("Error:");

    switch(err) {
    case OTA_DNS_LOOKUP_FALLIED:
        printf("DNS lookup has fallied\n");
        break;
    case OTA_SOCKET_ALLOCATION_FALLIED:
        printf("Impossible allocate required socket\n");
        break;
    case OTA_SOCKET_CONNECTION_FALLIED:
        printf("Server unreachable, impossible connect\n");
        break;
    case OTA_SHA_DONT_MATCH:
        printf("Sha256 sum does not fit downloaded sha256\n");
        break;
    case OTA_REQUEST_SEND_FALLIED:
        printf("Impossible send HTTP request\n");
        break;
    case OTA_DOWLOAD_SIZE_NOT_MATCH:
        printf("Dowload size don't match with server declared size\n");
        break;
    case OTA_ONE_SLOT_ONLY:
        printf("rboot has only one slot configured, impossible switch it\n");
        break;
    case OTA_FAIL_SET_NEW_SLOT:
        printf("rboot cannot switch between rom\n");
        break;
    case OTA_IMAGE_VERIFY_FALLIED:
        printf("Dowloaded image binary checsum is fallied\n");
        break;
    case OTA_UPDATE_DONE:
        printf("Ota has completed upgrade process, all ready for system software reset\n");
        break;
    case OTA_HTTP_OK:
        printf("HTTP server has response 200, Ok\n");
        break;
    case OTA_HTTP_NOTFOUND:
        printf("HTTP server has response 404, file not found\n");
        break;
    }
}

static void ota_task(void *pvParameters)
{


    while (1) {
    	xSemaphoreTake(wifi_alive, portMAX_DELAY);
    	char *path1 = NULL;
    	char *path2 = NULL;
    	int32 firmware_version = 0;

    	sysparam_get_string("path1",&path1);
    	sysparam_get_string("path2",&path2);
    	sysparam_get_int32("firmware_version",&firmware_version);

    	if(path1 == NULL && path2 == NULL && firmware_version == 0){
    		sysparam_set_int32("connection", CONNECTING);
    		sdk_system_restart();
    	}


    	ota_info info = {
			.server      = SERVER,
			.port      =  PORT,
		};

		info.binary_path = path1;
		info.sha256_path = path2;


		uint8 tries = 0;

		while(tries++ < 10) {

			OTA_err err;
			// Remake this task until ota work
			err = ota_update(&info);

			ota_error_handling(err);

			if(err != OTA_UPDATE_DONE) {
				vTaskDelayMs(1000);
				#ifdef DEBUG
					printf("\n\n\n");
				#endif


				continue;
			}
			sysparam_set_int32("version", firmware_version);
			vTaskDelayMs(1000);
			#ifdef DEBUG
				printf("Delay 1\n");
			#endif
			vTaskDelayMs(1000);
			#ifdef DEBUG
				printf("Delay 2\n");
			#endif
			vTaskDelayMs(1000);
			#ifdef DEBUG
				printf("Delay 3\n");
			#endif
			#ifdef DEBUG
				printf("Reset\n");
			#endif

			break;

		}
		sysparam_set_int32("connection", CONNECTING);
		sdk_system_restart();

    }
}

char *getOutletId() {

	char *outletId = (char *)malloc(22 * sizeof(char));
	uint8_t mac[6];
	sdk_wifi_get_macaddr(STATION_IF, mac);
	sprintf(outletId, "%s00%02x%02x%02x%02x%02x%02x01",DEVIC_HEADER, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return strupr(outletId);
	
}


static void  heap_task() {
	uint32_t freeheap = xPortGetFreeHeapSize();
	//#ifdef DEBUG
		printf("xPortGetFreeHeapSize = %d bytes\n", freeheap);
	//#endif

	if(freeheap < 3000){
		uint8 restart = 1;

		if (xQueueSend(restart_queue, &restart, 0) == pdFALSE) {
			#ifdef DEBUG
				printf("heap_task: Publish queue overflow.\r\n");
			#endif
			xQueueReset(restart_queue);
		}else{
			#ifdef DEBUG
				printf("heap_task: Publish queue successful.\r\n");
			#endif
		}
	}
}



static void  blink_task(void *pvParameters) {

    while(1) {
        switch(blink_mode) {
            case 0:
                gpio_write(LED, 1);
                vTaskDelayMs(500);
                gpio_write(LED, 0);
                vTaskDelayMs(500);
                break;
            case 1:
                gpio_write(LED, 1);
                vTaskDelayMs(500);
                break;
            case 2:
                gpio_write(LED, 1);
                vTaskDelayMs(250);
                gpio_write(LED, 0);
                vTaskDelayMs(250);
                break;
            default:
                vTaskDelayMs(500);
        }
    }
}

static void  reset_task() {
     if(reset_count >= 50) {
        blink_mode = 2;
        sysparam_set_int32("connection", SETTING);
        uint8 restart = 1;
        if (xQueueSend(restart_queue, &restart, 0) == pdFALSE) {
            #ifdef DEBUG
                printf("reset_task: Publish queue overflow.\r\n");
            #endif

            xQueueReset(restart_queue);
        }else{
            #ifdef DEBUG
                printf("reset_task: Publish queue successful.\r\n");
            #endif

        }
     }

     if(gpio_read(RESET_PIN) == 0) {
         reset_count++;

     }else{
         reset_count = 0;
     }




}

static void  topic_received(mqtt_message_data_t *md)
{



	Dictionary *dict = NULL;
	init_dictionary(&dict, mapping);


    mqtt_message_t *message = md->message;

	#ifdef DEBUG
		printf("Received: ");
		for(uint8 i = 0; i < md->topic->lenstring.len; ++i)
			printf("%c", md->topic->lenstring.data[ i ]);
		printf(" = ");
	#endif

    char msg[16];
    char command[13]={0};
    char cmd[3];
    char header[3];
    char param[9];
    memcpy(command,(char *)message->payload,12);

    command[12] = '\0';

	#ifdef DEBUG
		printf("%s\n", command);
	#endif


    sprintf(cmd,"%c%c",command[4], command[5]);
    sprintf(header,"%c%c",command[0], command[1]);
    sprintf(param,"%c%c%c%c%c%c%c%c",command[2], command[3], command[4], command[5], command[6], command[7], command[8], command[9]);

    if(strncmp(header,"55",2)==0 && (strncmp(cmd,"FF",2)==0 || strncmp(cmd,"ff",2)==0)) {
		uint8 hi = htoi(command[2]);
		uint8 lo = htoi(command[3]);
		device_address = hi*16 + lo;
		Set *pointer = sub_status[device_address]->set;
		while(pointer != NULL && pointer->key != NULL) {
			uint8 *zero_value = malloc(2*sizeof(uint8));
			memset(zero_value,0,2);
			sub_status[device_address]->setObject(sub_status[device_address], pointer->key, zero_value);
			free(zero_value);
			zero_value = NULL;
			pointer = pointer->next;
		}
		sysparam_set_int32("device_address", device_address);
		return;
	}

	if (strncmp(header,"54",2)==0){
		sub_list = strtoul(param, NULL, 16);
		sysparam_set_int32("sub_list", sub_list);
		return;
	}

	for(uint8 stage = 0; stage < 3; stage++) {
		switch(stage) {
			case 0:
				xSemaphoreTake(write_flag, portMAX_DELAY);
				if (xQueueSend(write_queue, command, 0) == pdFALSE) {
					#ifdef DEBUG
						printf("update_status_task: Publish queue overflow.\r\n");
					#endif

					xQueueReset(write_queue);
				}else{
					#ifdef DEBUG
						printf("update_status_task: Publish queue successful.\r\n");
					#endif

				}
				xSemaphoreGive(write_flag);

				break;
			case 1:
			{
				char *value = dict->getObject(dict, cmd);

				if(value == NULL) break;

				uint8 *raw = (uint8 *)malloc(PACKET_SIZE*sizeof(uint8));

				sprintf(command, "%s%02X%s%02X%02X","55",device_address,value,0x00,0x00);
				for(uint8 pos = 0, index = 0 ; pos < (PACKET_SIZE-1)*2; pos+=2, index++) {
					uint8 hi = htoi(command[pos]);
					uint8 lo = htoi(command[pos+1]);
					raw[index] = hi*16+lo;

				}

				uint8 *checksum = device->checksum(device, raw,PACKET_SIZE-1);
				raw[PACKET_SIZE-1] = *checksum;
				sprintf(command, "%s%02X",command,raw[5]);




				xSemaphoreTake(write_flag, portMAX_DELAY);
				if (xQueueSend(write_queue, command, 0) == pdFALSE) {
					#ifdef DEBUG
						printf("update_status_task: Publish queue overflow.\r\n");
					#endif

					xQueueReset(write_queue);
				}else{
					#ifdef DEBUG
						printf("update_status_task: Publish queue successful.\r\n");
					#endif

				}
				xSemaphoreGive(write_flag);

				free(checksum);
				checksum = NULL;

				free(raw);
				raw = NULL;
			}
				break;
			case 2:
				snprintf(msg, 16, "0");

				if (xQueueSend(publish_queue, msg, 0) == pdFALSE) {
					#ifdef DEBUG
						printf("topic_received: Publish queue overflow.\r\n");
					#endif
					xQueueReset(publish_queue);
				}else{
					#ifdef DEBUG
						printf("topic_received: Publish queue successful.\r\n");
					#endif

				}
				break;
		}
		vTaskDelayMs(450);

	}

	deinit_dictionary(&dict);



}

static void  topic_ota(mqtt_message_data_t *md)
{





    mqtt_message_t *message = md->message;

	#ifdef DEBUG
		printf("Received: ");
		for(uint8 i = 0; i < md->topic->lenstring.len; ++i)
			printf("%c", md->topic->lenstring.data[ i ]);

		printf(" = ");

	#endif

    char command[23];

    memcpy(command,(char *)message->payload,22);

    command[22] = '\0';




	#ifdef DEBUG
		printf("%s\n", command);
	#endif


    cJSON *json = cJSON_Parse(command);

    cJSON *version_value = cJSON_GetObjectItemCaseSensitive(json, "version");

    printf("version==>%s\n", version_value->valuestring);

    version = atoi(version_value->valuestring);



    sprintf(path1, "/fw/%s.bin", version_value->valuestring );
	#ifdef DEBUG
		printf("%s\n",path1);
	#endif

    sprintf(path2, "/fw/%s.sha256", version_value->valuestring);
	#ifdef DEBUG
		printf("%s\n",path2);
	#endif


    /*ota_info info = {
        .server      = SERVER,
		.port      =  PORT,
    };

    info.binary_path = path1;
    info.sha256_path = path2;*/

	sysparam_set_int32("connection",OTA);
    sysparam_set_string("path1",path1);
	sysparam_set_string("path2",path2);
	sysparam_set_int32("firmware_version",version);
	sdk_system_restart();



    /*if (xQueueSend(ota_queue, &info, 0) == pdFALSE) {
		//printf("topic_ota: Publish queue overflow.\r\n");
		xQueueReset(ota_queue);
	}else{
		//printf("topic_ota: Publish queue successful.\r\n");
	}*/
}


static void  mqtt_task(void *pvParameters)
{



    int ret         = 0;
    struct mqtt_network network;
    mqtt_client_t client   = mqtt_client_default;

    mqtt_packet_connect_data_t data = mqtt_packet_connect_data_initializer;
    char *outletId = getOutletId();

	char *subscribe[2];
	subscribe[0]= malloc(50*sizeof(char));
	sprintf(subscribe[0],"MEDOLE/MEDOLE/%s/req",outletId);
	subscribe[1]= malloc(50*sizeof(char));
	sprintf(subscribe[1],"MEDOLE/MEDOLE/%s/ota",outletId);

	char *publish[2];
	publish[0] = malloc(50*sizeof(char));
	sprintf(publish[0],"MEDOLE/MEDOLE/%s/raw",outletId);
	publish[1] = malloc(50*sizeof(char));
	sprintf(publish[1],"MEDOLE/MEDOLE/%s/connect",outletId);

    mqtt_network_new( &network );

    while(1) {

    	blink_mode = 0;

		char *ip = NULL;
		char *port = NULL;
		sysparam_get_string("ip", &ip);
		sysparam_get_string("port", &port);

		printf("ip==>%s,port==>%s\n",ip, port);

		xSemaphoreTake(wifi_alive, portMAX_DELAY);


		/*const struct addrinfo hints = {
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_STREAM,
		};
		struct addrinfo *res;
		#ifdef DEBUG
			printf("Running DNS lookup for %s...\r\n", ip);
		#endif
		int err = getaddrinfo(ip, 0, &hints, &res);

		if (err != 0 || res == NULL) {
			#ifdef DEBUG
				printf("DNS lookup failed err=%d res=%p\r\n", err, res);
			#endif
			if(res)
				freeaddrinfo(&res);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
			continue;
		}

		#ifdef DEBUG
			printf("%s: started\n\r", __func__);
		#endif
		#ifdef DEBUG
			printf("%s: (Re)connecting to MQTT server %s ... ",__func__,ip);
		#endif*/

		ret = mqtt_network_connect(&network, ip, atoi(port));
		if( ret ){
			#ifdef DEBUG
				printf("error: %d\n\r", ret);
			#endif

			taskYIELD();
			continue;
		}
		#ifdef DEBUG
			printf("done\n\r");
		#endif

		mqtt_client_new(&client, &network, 5000, mqtt_buf, 2048,
					  mqtt_readbuf, 512);


		data.willFlag = 1;
		data.will.topicName.cstring = MQTT_PUBTOPIC;
		data.will.message.cstring = MQTT_WILLMSG;
		data.will.qos = 1;
		data.will.retained = 0;
		data.MQTTVersion = 4;
		data.cleansession = 1;
		//data.clientID.cstring   = outletId;
		data.username.cstring   = MQTT_USER;
		data.password.cstring   = MQTT_PASS;
		data.keepAliveInterval  = 60;

		#ifdef DEBUG
			printf("Send MQTT connect ... ");
		#endif

		ret = mqtt_connect(&client, &data);
		if(ret){
			#ifdef DEBUG
				printf("error: %d\n\r", ret);
			#endif

			mqtt_network_disconnect(&network);

			taskYIELD();
			continue;
		}
		#ifdef DEBUG
			printf("done\r\n");
		#endif

		char *servers[] = {SNTP_SERVERS};
		/* Start SNTP */
		printf("Starting SNTP... ");
		/* SNTP will request an update each 5 minutes */
		sntp_set_update_delay(5*60000);
		/* Set GMT+1 zone, daylight savings off */
		const struct timezone tz = {8*60, 0};
		/* SNTP initialization */
		sntp_initialize(&tz);
		/* Servers must be configured right after initialization */
		sntp_set_servers(servers, sizeof(servers) / sizeof(char*));
		printf("DONE!\n");

		mqtt_subscribe(&client, subscribe[0], MQTT_QOS1, topic_received);
		mqtt_subscribe(&client, subscribe[1], MQTT_QOS1, topic_ota);

		xQueueReset(publish_queue);
		#ifdef DEBUG
			printf("\nMQTT connected!!\n");
		#endif


		char timestamp[9] = "\0";
		sprintf(timestamp,"%d",version);

		cJSON *monitor = cJSON_CreateObject();
		cJSON *version = cJSON_CreateString(timestamp);
		cJSON_AddItemToObject(monitor, "version", version);
		char *json = cJSON_PrintUnformatted(monitor);
		cJSON_Delete(monitor);


		mqtt_message_t message;
		message.payload = json;
		message.payloadlen = strlen(json);
		message.dup = 0;
		message.qos = MQTT_QOS1;
		message.retained = 0;



		ret = mqtt_publish(&client, publish[1], &message);
		free(json);
		json = NULL;


		if (ret != MQTT_SUCCESS ){
			#ifdef DEBUG
				printf("error while publishing message: %d\n", ret );
			#endif

			continue;
		}

		while(1){

			blink_mode = 1;

			char msg[PUB_MSG_LEN] = "\0";
			if(xQueueReceive(publish_queue, (void *)msg, 100) == pdTRUE){
				#ifdef DEBUG
					printf("mqtt_task: got message to publish\r\n");
				#endif

				if(atoi(msg)==0){
					cJSON *monitor = get_upload(device_address);
					char *json =  cJSON_PrintUnformatted(monitor);
					cJSON_Delete(monitor);
					mqtt_message_t message;
					message.payload = json;
					message.payloadlen = strlen(json);
					message.dup = 0;
					message.qos = MQTT_QOS1;
					message.retained = 0;
					ret = mqtt_publish(&client, publish[0], &message);
					free(json);
					json = NULL;

					if (ret != MQTT_SUCCESS ){
						#ifdef DEBUG
							printf("error while publishing message: %d\n", ret );
						#endif

						break;
					}

				}else{
					cJSON *monitor_array = cJSON_CreateArray();
					for(uint8 address = 1 ; address <= 16; address++) {
						uint32 temp = sub_list&(0x00000001<<(address-1));
						if(temp==0)
							continue;
						cJSON_AddItemToArray(monitor_array, get_upload(address));
					}

					char *json =  cJSON_PrintUnformatted(monitor_array);
					cJSON_Delete(monitor_array);
					mqtt_message_t message;
					message.payload = json;
					message.payloadlen = strlen(json);
					message.dup = 0;
					message.qos = MQTT_QOS1;
					message.retained = 0;
					ret = mqtt_publish(&client, publish[0], &message);
					free(json);
					json = NULL;

					if (ret != MQTT_SUCCESS ){
						#ifdef DEBUG
							printf("error while publishing message: %d\n", ret );
						#endif

						break;
					}

				}

			}else{
				#ifdef DEBUG
					printf("mqtt_task: No msg :(\n");
				#endif

			}

			ret = mqtt_yield(&client, 1000);
			if (ret == MQTT_DISCONNECTED)
				break;
		}
		#ifdef DEBUG
			printf("Connection dropped, request restart\n\r");
		#endif

		mqtt_network_disconnect(&network);
		taskYIELD();


    }
}

cJSON *get_upload(Device *device){

	uint8 index = 0;
	cJSON *slave = cJSON_CreateNumber(device->address);
	cJSON *co = JSON_CreateNumber(device->co);
	cJSON *co2 = cJSON_CreateNumber(*co2_value + *(co2_value+1)*256);
	cJSON *ch2o = cJSON_CreateNumber(*ch2o_value + *(ch2o_value+1)*256);
	cJSON *pm25 = cJSON_CreateNumber(*pm25_value + *(pm25_value+1)*256);
	cJSON *power_array = cJSON_CreateArray();
	cJSON *temperature_array = cJSON_CreateArray();
	cJSON *humidity_array = cJSON_CreateArray();
	cJSON *target_humidity = cJSON_CreateNumber(device->target_humidity);
	cJSON *fan = cJSON_CreateNumber(device->fan);
	cJSON *filter_acc = cJSON_CreateNumber(device->fliter_acc);
	cJSON *filter_spc = cJSON_CreateNumber(device->filter_spc);
	cJSON *fh_mode = cJSON_CreateNumber(device->fh_mode);
	cJSON *fh_auto = cJSON_CreateNumber(device->fh_auto);
	cJSON *fh_state = cJSON_CreateNumber(device->fh_state);
	cJSON *sel6 = cJSON_CreateNumber(device->sel6);
	cJSON *monitor = cJSON_CreateObject();
	cJSON *err = NULL;
	char err_value[9] = "\0";
	sprintf(err_value, "%04X%04X", device->err[0], device->err[1]);
	err = cJSON_CreateString(err_value);
	
	for(index = 0; index < sizeof(device->power);index++) 
		cJSON_AddItemToArray(power_array, cJSON_CreateNumber(device->power[index]));
	
	for(index = 0; index < sizeof(device->temperature);index++) 
		cJSON_AddItemToArray(temperature_array, cJSON_CreateNumber(device->temperature[index]));
	
	for(index = 0; index < sizeof(device->humidity);index++) 
		cJSON_AddItemToArray(temperature_array, cJSON_CreateNumber(device->humidity[index]));
	
	cJSON_AddItemToObject(monitor, "POWER", power_array);
	cJSON_AddItemToObject(monitor, "SLAVE", slave);
	cJSON_AddItemToObject(monitor, "CO", co);
	cJSON_AddItemToObject(monitor, "CO2", co2);
	cJSON_AddItemToObject(monitor, "CH2O", ch2o);
	cJSON_AddItemToObject(monitor, "PM25", pm25);
	cJSON_AddItemToObject(monitor, "FILTER_ACC", filter_acc);
	cJSON_AddItemToObject(monitor, "FILTER_SPC", filter_spc);
	cJSON_AddItemToObject(monitor, "T", temperature_array);
	cJSON_AddItemToObject(monitor, "H", humidity_array);
	cJSON_AddItemToObject(monitor, "HUMIDITY", target_humidity);
	cJSON_AddItemToObject(monitor, "FAN", fan);
	cJSON_AddItemToObject(monitor, "ERR", err);
	cJSON_AddItemToObject(monitor, "FHMODE", fh_mode);
	cJSON_AddItemToObject(monitor, "FHAUTO", fh_auto);
	cJSON_AddItemToObject(monitor, "FHSTATE", fh_state);
	cJSON_AddItemToObject(monitor, "SEL6", sel6);

	return monitor;

}

static void  wifi_task(void *pvParameters) {


    while(1) {

		uint8_t current_status  = 0;

		char *ssid = NULL;
		char *password = NULL;
		sysparam_get_string("ssid", &ssid);
		sysparam_get_string("password", &password);



		struct sdk_station_config config;
		memset(&config, 0, sizeof(config));

		if(ssid != NULL)
			strncpy((char *)config.ssid, ssid, sizeof(config.ssid));
		if(password != NULL)
			strncpy((char *)config.password, password, sizeof(config.password));

		//printf("WiFi: connecting to WiFi\n\r");
		sdk_wifi_set_opmode(STATION_MODE);
		sdk_wifi_station_set_auto_connect(true);
		sdk_wifi_station_set_config(&config);
		sdk_wifi_station_connect();
		current_status = sdk_wifi_station_get_connect_status();

		while (current_status != STATION_GOT_IP){

			current_status = sdk_wifi_station_get_connect_status();
			#ifdef DEBUG
				printf("%s: current_status = %d\n\r", __func__, current_status );
			#endif

			if( current_status == STATION_WRONG_PASSWORD ){
				#ifdef DEBUG
					printf("WiFi: wrong password\n\r");
				#endif

				break;
			} else if( current_status == STATION_NO_AP_FOUND ) {
				#ifdef DEBUG
					printf("WiFi: AP not found\n\r");
				#endif

				break;
			} else if( current_status == STATION_CONNECT_FAIL ) {
				#ifdef DEBUG
					printf("WiFi: connection failed\r\n");
				#endif

				break;
			}
			vTaskDelay( 1000 / portTICK_PERIOD_MS );

		}

		if (current_status == STATION_GOT_IP) {
			#ifdef DEBUG
				printf("WiFi: Connected\n\r");
			#endif

			//xSemaphoreGive( wifi_alive );
			taskYIELD();
		}


		while ((current_status = sdk_wifi_station_get_connect_status()) == STATION_GOT_IP) {
			xSemaphoreGive( wifi_alive );
			taskYIELD();
		}
		#ifdef DEBUG
			printf("WiFi: disconnected\n\r");
		#endif
		sdk_wifi_station_disconnect();
		//sdk_wifi_station_disconnect();
		vTaskDelay( 1000 / portTICK_PERIOD_MS );

	}
}

int ICACHE_FLASH_ATTR  getDevInfo(HttpdConnData *connData) {
	char devinfo[500] = "\0";

	uint8_t ap_mac[6];
	uint8_t sta_mac[6];
	sdk_wifi_get_macaddr(SOFTAP_IF, ap_mac);
	sdk_wifi_get_macaddr(STATION_IF, sta_mac);
	char mac_value[18]={0};
	sprintf(mac_value,MACSTR,MAC2STR(sta_mac));

	sdk_wifi_get_macaddr(STATION_IF, sta_mac);
	char oui_str[4]={"\0"};
	sprintf(oui_str,"%02X%02X%02X",sta_mac[0],sta_mac[1],sta_mac[2]);

	cJSON *root = cJSON_CreateObject();
	cJSON *status = cJSON_CreateNumber(1);

	cJSON *oui = cJSON_CreateString(oui_str);
	cJSON *serial = cJSON_CreateString("E803");
	cJSON *interface = cJSON_CreateArray();



	char ifnames[3][7] = {"br0", "ra0", "apcli0"};
	cJSON *ip = cJSON_CreateString("192.168.240.1");
	cJSON *netmask = cJSON_CreateString("255.255.255.0");



	for(uint8 index = 0; index < 3; index++) {
		cJSON *interface_item = cJSON_CreateObject();
		cJSON *ifname = cJSON_CreateString(ifnames[index]);
		cJSON *mac = cJSON_CreateString(mac_value);

		cJSON_AddItemToObject(interface_item, "ifname", ifname);
		cJSON_AddItemToObject(interface_item, "mac", mac);
		if(strncmp("br0",ifnames[index],strlen(ifnames[index])) == 0){
			cJSON_AddItemToObject(interface_item, "ip", ip);
			cJSON_AddItemToObject(interface_item, "netmask", netmask);
		}


		cJSON_AddItemToArray(interface, interface_item);

	}
	cJSON_AddItemToObject(root,"status",status);
	cJSON_AddItemToObject(root,"oui",oui);
	cJSON_AddItemToObject(root,"serial",serial);
	cJSON_AddItemToObject(root,"interface",interface);

	cJSON_PrintPreallocated(root, devinfo, 500, false);
	cJSON_Delete(root);

	if (connData->conn==NULL) {
			//Connection aborted. Clean up.
			return HTTPD_CGI_DONE;
	}

	if (connData->requestType!=HTTPD_METHOD_GET &&  connData->requestType!=HTTPD_METHOD_POST) {
		//Sorry, we only accept GET requests.
		httpdStartResponse(connData, 405);  //http error code 'unacceptable'
		httpdEndHeaders(connData);
		return HTTPD_CGI_DONE;
	}

	//Generate the header
	//We want the header to start with HTTP code 200, which means the document is found.
	httpdStartResponse(connData, 200);
	//We are going to send some HTML.
	httpdHeader(connData, "Content-Type", "application/json");
	//No more headers.
	httpdEndHeaders(connData);

	//We're going to send the HTML as two pieces: a head and a body. We could've also done
	//it in one go, but this demonstrates multiple ways of calling httpdSend.
	//Send the HTML head. Using -1 as the length will make httpdSend take the length
	//of the zero-terminated string it's passed as the amount of data to send.
	httpdSend(connData, devinfo, -1);

	//All done.
	return HTTPD_CGI_DONE;


}

int ICACHE_FLASH_ATTR  getSiteSurvey(HttpdConnData *connData) {

	if (connData->conn==NULL) {
			//Connection aborted. Clean up.
			return HTTPD_CGI_DONE;
	}

	if (connData->requestType!=HTTPD_METHOD_GET && connData->requestType!=HTTPD_METHOD_POST) {
		//Sorry, we only accept GET requests.
		httpdStartResponse(connData, 405);  //http error code 'unacceptable'
		httpdEndHeaders(connData);
		return HTTPD_CGI_DONE;
	}

	//Generate the header
	//We want the header to start with HTTP code 200, which means the document is found.
	httpdStartResponse(connData, 200);
	//We are going to send some HTML.
	httpdHeader(connData, "Content-Type", "application/json");
	//No more headers.
	httpdEndHeaders(connData);

	//We're going to send the HTML as two pieces: a head and a body. We could've also done
	//it in one go, but this demonstrates multiple ways of calling httpdSend.
	//Send the HTML head. Using -1 as the length will make httpdSend take the length
	//of the zero-terminated string it's passed as the amount of data to send.
	httpdSend(connData, rssiInfo, -1);

	//All done.
	return HTTPD_CGI_DONE;


}

int ICACHE_FLASH_ATTR  setSrvInfo(HttpdConnData *connData) {

	if (connData->conn==NULL) {
			//Connection aborted. Clean up.
			return HTTPD_CGI_DONE;
	}

	if (connData->requestType!=HTTPD_METHOD_GET && connData->requestType!=HTTPD_METHOD_POST) {
		//Sorry, we only accept GET requests.
		httpdStartResponse(connData, 405);  //http error code 'unacceptable'
		httpdEndHeaders(connData);
		return HTTPD_CGI_DONE;
	}



	cJSON *data = cJSON_Parse(connData->post->buff);
	cJSON *server = cJSON_GetObjectItemCaseSensitive(data, "server");
	cJSON *param = cJSON_GetObjectItemCaseSensitive(data, "para");
	cJSON *ip = cJSON_GetObjectItemCaseSensitive(server, "mqtt");
	cJSON *port = cJSON_GetObjectItemCaseSensitive(server, "mqtt_port");
	cJSON *timestamp = cJSON_GetObjectItemCaseSensitive(param, "timestamp");
	cJSON *interval = cJSON_GetObjectItemCaseSensitive(param, "interval");

	if(data == NULL || server == NULL || param == NULL || ip == NULL || port == NULL || timestamp == NULL || interval == NULL) {
		//http error code 'unacceptable'
		httpdStartResponse(connData, 405);
		//We are going to send some HTML.
		httpdHeader(connData, "Content-Type", "application/json");
		//No more headers.
		httpdEndHeaders(connData);
		return HTTPD_CGI_DONE;
	}
	sysparam_set_string("ip", ip->valuestring);
	sysparam_set_string("port", port->valuestring);
	sysparam_set_string("timestamp", timestamp->valuestring);
	sysparam_set_string("interval", interval->valuestring);


	//Generate the header
	//We want the header to start with HTTP code 200, which means the document is found.
	httpdStartResponse(connData, 200);
	//We are going to send some HTML.
	httpdHeader(connData, "Content-Type", "application/json");
	//No more headers.
	httpdEndHeaders(connData);

	/*"para":{
			"timestamp":"1800",
			"interval":"15"
			}*/



	//httpdSend(connData,connData->post->buff, -1);
	//All done.

	return HTTPD_CGI_DONE;

}

int ICACHE_FLASH_ATTR  setConnection(HttpdConnData *connData) {

	if (connData->conn==NULL) {
			//Connection aborted. Clean up.
			return HTTPD_CGI_DONE;
	}

	if (connData->requestType!=HTTPD_METHOD_GET && connData->requestType!=HTTPD_METHOD_POST) {
		//Sorry, we only accept GET requests.
		httpdStartResponse(connData, 405);  //http error code 'unacceptable'
		httpdEndHeaders(connData);
		return HTTPD_CGI_DONE;
	}

	//Generate the header
	//We want the header to start with HTTP code 200, which means the document is found.
	httpdStartResponse(connData, 200);
	//We are going to send some HTML.
	httpdHeader(connData, "Content-Type", "application/json");
	//No more headers.
	httpdEndHeaders(connData);

	//We're going to send the HTML as two pieces: a head and a body. We could've also done
	//it in one go, but this demonstrates multiple ways of calling httpdSend.
	//Send the HTML head. Using -1 as the length will make httpdSend take the length
	//of the zero-terminated string it's passed as the amount of data to send.

	cJSON *data = cJSON_Parse(connData->post->buff);
	cJSON *conn = cJSON_GetObjectItemCaseSensitive(data, "conn");
	cJSON *ap = cJSON_GetObjectItemCaseSensitive(data, "ap");
	cJSON *ssid = cJSON_GetObjectItemCaseSensitive(ap, "ssid");
	cJSON *password = cJSON_GetObjectItemCaseSensitive(ap, "passwd");

	if(data == NULL || conn == NULL || ap == NULL || ssid == NULL) {
		//http error code 'unacceptable'
		httpdStartResponse(connData, 405);
		httpdEndHeaders(connData);
		return HTTPD_CGI_DONE;
	}

	sysparam_set_int32("connection", conn->valueint);
	sysparam_set_string("ssid", ssid->valuestring);
	sysparam_set_string("password", password->valuestring);

	uint8 restart = 1;

	if (xQueueSend(restart_queue, &restart, 1000) == pdFALSE) {
		#ifdef DEBUG
			printf("setConnection: Publish queue overflow.\r\n");
		#endif

		xQueueReset(restart_queue);
	}else{

		#ifdef DEBUG
			printf("setConnection: Publish queue successful.\r\n");
		#endif

	}



	//httpdSend(connData,connData->post->buff, -1);
	//All done.
	return HTTPD_CGI_DONE;

}



void scanDoneCallback(void * arg, sdk_scan_status_t current_status) {

	char* auth[] ={"OPEN","WEP","WPA","WPA2","WPA+WPA2"};


	uint8 iCount=0;
	cJSON *root =  cJSON_CreateObject();
	cJSON *status = NULL;
	cJSON *ap =  cJSON_CreateArray();
	if(current_status == SCAN_OK) {

		struct sdk_bss_info *bss_link = (struct sdk_bss_info *)arg;
		bss_link = bss_link->next.stqe_next;


		while(bss_link != NULL && iCount <= 10) {
			char rssi_str[8];
			char channel_str[3];
			sprintf(rssi_str, "%d",  bss_link->rssi);
			sprintf(channel_str, "%d", bss_link->channel);
			cJSON *ap_item =  cJSON_CreateObject();
			cJSON *channel = cJSON_CreateString(channel_str);
			cJSON *ssid = cJSON_CreateString((char *)bss_link->ssid);
			char bssid_value[18] = {0};
			sprintf(bssid_value,MACSTR,MAC2STR(bss_link->bssid));
			cJSON *bssid = cJSON_CreateString(bssid_value);
			cJSON *security = cJSON_CreateString(auth[bss_link->authmode]);
			cJSON *signal = cJSON_CreateString(rssi_str);
			cJSON *mode = cJSON_CreateString("11b/g");
			cJSON_AddItemToObject(ap_item,"channel",channel);
			cJSON_AddItemToObject(ap_item,"ssid",ssid);
			cJSON_AddItemToObject(ap_item,"bssid",bssid);
			cJSON_AddItemToObject(ap_item,"security",security);
			cJSON_AddItemToObject(ap_item,"signal",signal);
			cJSON_AddItemToObject(ap_item,"mode",mode);

			cJSON_AddItemToArray(ap, ap_item);

			bss_link = bss_link->next.stqe_next;
			iCount++;
		}
		status = cJSON_CreateNumber(1);

	} else{
		status = cJSON_CreateNumber(0);

	}
	cJSON_AddItemToObject(root, "status", status);
	cJSON_AddItemToObject(root, "ap", ap);

	cJSON_PrintPreallocated(root, rssiInfo, 2000, false);
	#ifdef DEBUG
		printf("%s\n", rssiInfo);
	#endif

	cJSON_Delete(root);




}

void dhcpInit()
{
	char id[7];
	char apName[LENGTH];
	uint8_t mac[6];
	struct ip_info info;
	sdk_wifi_station_disconnect();
	sdk_wifi_set_opmode(SOFTAP_MODE); //Set softAP + station mode

	IP4_ADDR(&info.ip, 192, 168, 240, 1);
	IP4_ADDR(&info.gw, 192, 168, 240, 1);
	IP4_ADDR(&info.netmask, 255, 255, 255, 0);
	sdk_wifi_set_ip_info(SOFTAP_IF, &info);
	sdk_wifi_get_macaddr(SOFTAP_IF, mac);
	sprintf(id,"%02x%02x%02x",mac[3],mac[4],mac[5]);
	sprintf(apName, "GJ_%s", id);
	//strupr(apName);

	#ifdef DEBUG
		printf(apName);
	#endif

	struct sdk_softap_config config = {	.ssid_hidden = 0,
										.channel = 3,
										.authmode = AUTH_OPEN,
										.max_connection = 3,
										.beacon_interval = 100, };
	sprintf((char *)config.ssid,"%s",apName);

	sdk_wifi_softap_set_config(&config); // Set ESP8266 soft-AP config
	ip_addr_t first_client_ip;
	IP4_ADDR(&first_client_ip, 192, 168, 240, 2);
	dhcpserver_start(&first_client_ip, 4);
	dhcpserver_set_dns(&info.ip);
	dhcpserver_set_router(&info.ip);



}

void settingTask(void *pvParameters) {
	bool init = false;
	while(1) {


		if(!init) {
			//printf("\nInit.\n");
			dhcpInit();
			sdk_wifi_set_opmode(STATIONAP_MODE);
			init = true;
		}

		sdk_wifi_station_scan(NULL,scanDoneCallback);
		vTaskDelay(10000 / portTICK_PERIOD_MS);



	}



	vTaskDelete(NULL);
}



void restart_task(void *pvParameters)
{
	QueueHandle_t *queue = (QueueHandle_t *)pvParameters;
    while(1) {

    	uint8 restart = 0;
		if(xQueueReceive(*queue, &restart, 100)) {
			vTaskDelay(5000 / portTICK_PERIOD_MS);
			sdk_system_restart();


		} else {
			#ifdef DEBUG
				printf("restart_task: No msg :(\n");
			#endif

		}

		taskYIELD();
    }
    vTaskDelete(NULL);
}

void write_uart_task(void *pvParameters){
	QueueHandle_t *queue = (QueueHandle_t *)pvParameters;
	while(1) {

		Packet packet;
		memset(&packet, sizeof(Packet));
		if(xQueueReceive(*queue, &packet, 100)) {


			write();


		} else {
			#ifdef DEBUG
				printf("write_uart_task: No msg :(\n");
			#endif

		}
	}
	vTaskDelete(NULL);
}

void update_status_task(void *pvParameters){
	QueueHandle_t *queue = (QueueHandle_t *)pvParameters;
	uint8_t count = 0;

	while(1) {



		Set *pointer = cmd;


		while(pointer != NULL && pointer->key != NULL) {

				char command[13]={0};
				uint8 *raw = (uint8 *)malloc(PACKET_SIZE*sizeof(uint8));
				sprintf(command, "%s%02X%s%02X%02X","55",device_address,pointer->key,0x00,0x00);
				for(uint8 pos = 0, index = 0 ; pos < (PACKET_SIZE-1)*2; pos+=2, index++) {
					uint8 hi = htoi(command[pos]);
					uint8 lo = htoi(command[pos+1]);
					raw[index] = hi*16+lo;

				}

				uint8 *checksum = device->checksum(device, raw,PACKET_SIZE-1);
				raw[PACKET_SIZE-1] = *checksum;
				sprintf(command, "%s%02X",command,raw[5]);


				xSemaphoreTake(write_flag, portMAX_DELAY);
				if (xQueueSend(*queue, command, 0) == pdFALSE) {
					#ifdef DEBUG
						printf("update_status_task: Publish queue overflow.\r\n");
					#endif

					xQueueReset(*queue);
				}else{
					#ifdef DEBUG
						printf("update_status_task: Publish queue successful.\r\n");
					#endif

				}
				xSemaphoreGive(write_flag);

				free(checksum);
				checksum = NULL;

				free(raw);
				raw = NULL;



				vTaskDelay(250/portTICK_PERIOD_MS);




				pointer++;


		}

		char msg[16];
		snprintf(msg, 16, "0");
		if (xQueueSend(publish_queue, (void *)msg, 0) == pdFALSE) {
			#ifdef DEBUG
				//printf("update_status_task: Publish queue overflow.\r\n");
			#endif

			xQueueReset(publish_queue);
		}else{
			#ifdef DEBUG
				//printf("update_status_task: Publish queue successful.\r\n");
			#endif

		}
		vTaskDelay(2000/portTICK_PERIOD_MS);


		pointer = cmd;

		if(device_address != count+1 && (sub_list & (0x00000001<<count))) {
			while(pointer != NULL && pointer->key != NULL) {

				char command[13]={0};
				uint8 *raw = (uint8 *)malloc(PACKET_SIZE*sizeof(uint8));
				sprintf(command, "%s%02X%s%02X%02X","55",count+1,pointer->key,0x00,0x00);
				for(uint8 pos = 0, index = 0 ; pos < (PACKET_SIZE-1)*2; pos+=2, index++) {
					uint8 hi = htoi(command[pos]);
					uint8 lo = htoi(command[pos+1]);
					raw[index] = hi*16+lo;

				}



				uint8 *checksum = device->checksum(device, raw,PACKET_SIZE-1);
				raw[PACKET_SIZE-1] = *checksum;
				sprintf(command, "%s%02X",command,raw[5]);


				xSemaphoreTake(write_flag, portMAX_DELAY);
				if (xQueueSend(*queue, command, 0) == pdFALSE) {
					#ifdef DEBUG
						printf("update_status_all_task: Publish queue overflow.\r\n");
					#endif

					xQueueReset(*queue);
				}else{
					#ifdef DEBUG
						printf("update_status_all_task: Publish queue successful.\r\n");
					#endif

				}
				xSemaphoreGive(write_flag);

				free(checksum);
				checksum = NULL;

				free(raw);
				raw = NULL;



				vTaskDelay(250/portTICK_PERIOD_MS);




				pointer++;


			}
			if (sdk_wifi_station_get_connect_status() == STATION_GOT_IP) {
				/* Print date and time each 5 minute */
				time_t ts = time(NULL);
				printf("TIME: %s", ctime(&ts));
				struct tm * timeinfo;

				time (&ts);
				timeinfo = localtime (&ts);


				char command[13]="\0";
				uint8 raw[6]={0x00};
				sprintf(command,"%s%02XC3%02X%02X","55",device_address,timeinfo->tm_hour,timeinfo->tm_min);
				for(uint8 pos = 0, index = 0 ; pos < (PACKET_SIZE-1)*2; pos+=2, index++) {
					uint8 hi = htoi(command[pos]);
					uint8 lo = htoi(command[pos+1]);
					raw[index] = hi*16+lo;
				}
				uint8 *checksum = device->checksum(device, raw,PACKET_SIZE-1);
				raw[PACKET_SIZE-1] = *checksum;
				sprintf(command, "%s%02X",command,raw[5]);

				if (xQueueSend(write_queue, command, 0) == pdFALSE) {
					#ifdef DEBUG
						printf("update_status_task: Publish queue overflow.\r\n");
					#endif

					xQueueReset(write_queue);
				}else{
					#ifdef DEBUG
						printf("update_status_task: Publish queue successful.\r\n");
					#endif

				}
				free(checksum);
				checksum = NULL;
				vTaskDelay(250/portTICK_PERIOD_MS);

				sprintf(command,"%s%02XC9%02X00","55",device_address,timeinfo->tm_wday);
				for(uint8 pos = 0, index = 0 ; pos < (PACKET_SIZE-1)*2; pos+=2, index++) {
					uint8 hi = htoi(command[pos]);
					uint8 lo = htoi(command[pos+1]);
					raw[index] = hi*16+lo;
				}

				checksum = device->checksum(device, raw,PACKET_SIZE-1);
				raw[PACKET_SIZE-1] = *checksum;
				sprintf(command, "%s%02X",command,raw[5]);

				if (xQueueSend(write_queue, command, 0) == pdFALSE) {
					#ifdef DEBUG
						printf("update_status_task: Publish queue overflow.\r\n");
					#endif

					xQueueReset(write_queue);
				}else{
					#ifdef DEBUG
						printf("update_status_task: Publish queue successful.\r\n");
					#endif

				}
				free(checksum);
				checksum = NULL;
				vTaskDelay(250/portTICK_PERIOD_MS);
			}

			char msg[16];
			snprintf(msg, 16, "1");
			if (xQueueSend(publish_queue, (void *)msg, 0) == pdFALSE) {
				#ifdef DEBUG
					//printf("update_status_task: Publish queue overflow.\r\n");
				#endif

				xQueueReset(publish_queue);
			}else{
				#ifdef DEBUG
					//printf("update_status_task: Publish queue successful.\r\n");
				#endif

			}

			vTaskDelay(2000/portTICK_PERIOD_MS);
		}



		//char str[17] = "\0";

		//sprintf(str, "%X",sub_list);

		count = (count + 1) % 16;





	}

	vTaskDelete(NULL);

}






void read_uart_task(void *pvParameters){


	uint8 buf_size = 0xFF;
	uint8 buffer[buf_size];
	uint8 byte = 0;
	uint8 pointer = 0;

	bool flag = false;

	while(1) {
		//printf("\nREADING UART.\n");

		if (read(0, (void*)&byte, 1)) { // 0 is stdin

			if(byte == 0x55 && flag == false) {
				pointer = 0;
				memset(buffer , 0 , sizeof(buffer));
			}

			buffer[pointer++] = byte;



			if(pointer == 1 ) {
				if(buffer[0] == 0x55)
					flag = true;
				else {
					pointer = 0;
					flag = false;
				}

			}




			if(flag == true){

				if(pointer == PACKET_SIZE) {
					#ifdef DEBUG
						printf("RECEIVED==>");
						for(int index =0; index < pointer; index++){
							printf("%02x", buffer[index]);
						}
						printf("\n");
					#endif


					uint8 packet[PACKET_SIZE];

					for(uint8 index = 0; index < PACKET_SIZE; index++) {
						packet[index] =buffer[index];
					}


					uint8 cmd = packet[PACKET_CMD];

					uint8 address = packet[PACKET_ADDRESS];

					//printf("address:%d\n", address);
					uint8 *checksum = device->checksum(device, packet,PACKET_SIZE-1);
					if (address >=1 && address <=32 && *checksum == packet[PACKET_CHECKSUM]){

						char key[3]={"\0"};
						sprintf(key,"%02X",cmd);
						uint8 *data = (uint8 *)malloc(2*sizeof(uint8));
						*data = packet[PACKET_DAT0];
						*(data+1) = packet[PACKET_DAT1];

						sub_status[address-1]->setObject(sub_status[address-1], key, data);
						free(data);
					}

					free(checksum);
					pointer = 0;
					flag = false;
					memset(buffer, 0, sizeof(buffer));

				}

			}

		}
	}


	vTaskDelete(NULL);
}




void send_command_impl(Device *self, Packet *packet) {

	write(1,(uint8_t *)packet, sizeof(packet));

}

uint8 *checksum_impl(Device *self, Packet *packet) {
	uint8 length = 0;
	uint8 *checksum = (uint8 *)malloc(sizeof(uint8));

	*checksum = 0;
	for(uint8 index =0; index < size; index++){
		*checksum ^= *(data +index);
	}
	return checksum;
}




void user_init(void)
{
    uart_set_baud(0, 9600);
	#ifdef DEBUG
		printf("SDK version:%s\n", sdk_system_get_sdk_version());
	#endif

	/* Set to 1.5 stopbits */
	//uart_set_stopbits(0, UART_STOPBITS_1);

	/* Enable parity bit */
	//uart_set_parity_enabled(0, false);


		/* Get data */
	//uint8 baud = uart_get_baud(0);
	//UART_StopBits stopbits = uart_get_stopbits(0);
	//bool parity_enabled = uart_get_parity_enabled(0);
	//UART_Parity parity = uart_get_parity(0);

	/* Print to UART0 */
	#ifdef DEBUG
		//printf("Baud: %d\n", baud);
	#endif

	#ifdef DEBUG
			/*switch(stopbits){
				case UART_STOPBITS_0:
					printf("Stopbits: 0\n");
				break;
				case UART_STOPBITS_1:
					printf("Stopbits: 1\n");
				break;
				case UART_STOPBITS_1_5:
					printf("Stopbits: 1.5\n");
				break;
				case UART_STOPBITS_2:
					printf("Stopbits: 2\n");
				break;
				default:
					printf("Stopbits: Error\n");
				break;
			}*/
	#endif


	#ifdef DEBUG
		//printf("Parity bit enabled: %d\n", parity_enabled);
	#endif



	#ifdef DEBUG
		/*switch(parity){
			case UART_PARITY_EVEN:
				printf("Parity: Even");
			break;
			case UART_PARITY_ODD:
				printf("Parity: Odd");
			break;
			default:
				printf("Parity: Error");
			break;
		}*/
	#endif

	gpio_enable(RESET_PIN, GPIO_INPUT);
	gpio_enable(LED, GPIO_OUTPUT);




	restart_queue = xQueueCreate(3,sizeof(uint8));



	xTaskCreate(&restart_task, "restart", 512, &restart_queue, 2, NULL);



    uint32 conn = 0;
    sysparam_get_int32("sub_list", &sub_list);
    sysparam_get_int32("connection", &conn);
    sysparam_get_int32("version", &version);
    sysparam_get_int32("device_address", &device_address);




    switch(conn){
    	case SETTING:
    		xTaskCreate(&blink_task, "blink", 512, NULL, 2 ,NULL);
    		httpdInit(builtInUrls, 80);
			rssiInfo = (char*)malloc(2000*sizeof(char));
			xTaskCreate(&settingTask, "setting", 512, NULL, 2, NULL);
    		break;
    	case CONNECTING:
    	{
    		xTaskCreate(&blink_task, "blink", 512, NULL, 2 ,NULL);
    		init_device(&device, send_command_impl,checksum_impl);


			/*for(uint8 index = 0; index < 16; index++) {
				init_dictionary(&sub_status[index], cmd);
			}*/

			Set *pointer = sub_status[device_address]->set;
			while(pointer != NULL && pointer->key != NULL) {
				uint8 *zero_value = malloc(2*sizeof(uint8));
				memset(zero_value,0xFF,2);
				sub_status[device_address]->setObject(sub_status[device_address], pointer->key, zero_value);
				free(zero_value);
				zero_value = NULL;
				pointer = pointer->next;
			}


			vSemaphoreCreateBinary(wifi_alive);
			vSemaphoreCreateBinary(write_flag);

			publish_queue = xQueueCreate(10, 16);
			write_queue = xQueueCreate(3, 13 * sizeof(char));

    		xTaskCreate(&wifi_task, "wifi_task",  512, NULL, 2, NULL);


			xTaskCreate(&mqtt_task, "mqtt_task", 1024, NULL, 3, NULL);


			xTaskCreate(&read_uart_task, "readuart", 512, NULL, 4, NULL);
			xTaskCreate(&write_uart_task, "writeuart", 512, &write_queue, 5, NULL);
			xTaskCreate(&update_status_task, "updatestatus", 512, &write_queue, 6 ,NULL);


			sdk_os_timer_setfn(&heap_timer,(sdk_os_timer_func_t*)heap_task,NULL);
			sdk_os_timer_setfn(&reset_timer,(sdk_os_timer_func_t*)reset_task,NULL);


			sdk_os_timer_arm(&heap_timer, 10000, 1);
			sdk_os_timer_arm(&reset_timer, 100, 1);
    	}
    		break;
    	case OTA:
    		vSemaphoreCreateBinary(wifi_alive);
    		xTaskCreate(&wifi_task, "wifi_task",  256, NULL, 2, NULL);
    		xTaskCreate(&ota_task, "ota_task", 512, NULL, 3, NULL);
    		break;
    }










}
