
#include <glib.h>
#include <glib-object.h>
#include <axsdk/axevent.h>
#include <syslog.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <syslog.h>

#include <sys/time.h>
#include <capture.h>

#define EVENT_WINDOW_KEY    "window"
#define EVENT_MOTION_KEY    "motion"

const int DETECTION_EVENT = 0;
const int IMAGE_EVENT = 1;

const int MOTION_TYPE = 0;
const AUDIO_TYPE = 1;

static int connfd = -1;
static media_stream *stream;

static void
motion_subscription_callback(guint subscription,
    AXEvent *event, guint *token);

static guint
subscribe_to_motion(AXEventHandler *event_handler, guint *token);


static uint32_t readInt()
{
	int bytesToRead = sizeof(uint32_t);
	unsigned char buff[bytesToRead];
	int bytesRead = 0, r = 0;
	while(bytesToRead > 0)
	{
		r = read(connfd, buff + bytesRead, bytesToRead);
		if(r < 0)
		{
			break;
		}
		bytesRead += r;
		bytesToRead -= r;
	}

	uint32_t *resultP = (uint32_t* ) buff;
	// ntohl stands for "network to host long"
	// The opposite of htonl
	uint32_t result = ntohl(*resultP);
	return result;
}

static void send_detection_event(int type)
{
	if(connfd >= 0)
	{
		int timestamp = htonl(12345); // TODO take system timestamp
		int detectionEventHtonl = htonl(DETECTION_EVENT);
		int typeEventHtonl = htonl(type);

		//TODO mutex
		write(connfd, &detectionEventHtonl, sizeof(detectionEventHtonl));
		write(connfd, &typeEventHtonl, sizeof(typeEventHtonl));
		write(connfd, &timestamp, sizeof(timestamp));
	}
}

static void send_motion_event()
{
	send_detection_event(MOTION_TYPE);
}

static void send_audio_event()
{
	send_detection_event(AUDIO_TYPE);
}

static void sendImageFromStream()
{
	// SEND IMAGES
	media_stream *stream;
//	Open the stream
	stream = capture_open_stream(IMAGE_JPEG, "resolution=160x90&fps=15");
	syslog(LOG_INFO, "Open stream captured 1000, connfd %d", connfd);

	syslog(LOG_INFO, "enter");
	media_frame *frame;
	void *data;

	frame = capture_get_frame(stream);
	syslog(LOG_INFO, "here");
	data = capture_frame_data(frame);
	
	size_t size = capture_frame_size(frame); 
	syslog(LOG_INFO, "Image captured");
	
	int total_size = size;
	syslog(LOG_INFO, "Total size %d", total_size);
	total_size = htonl(total_size);

//	Then send the data of the image
	int row = 0;
	unsigned char rowData[size];
	for (row = 0; row < size; row++)
	{
			rowData[row] = ((unsigned char *) data)[row];
	}
		
//	g_mutex_lock(mutex);

	write(connfd, &total_size, sizeof(total_size));
	write(connfd, rowData, sizeof(rowData));
//	g_mutex_unlock(mutex);
}

static void
motion_subscription_callback(guint subscription,
    AXEvent *event, guint *token)
{
	syslog(LOG_INFO, "CALLBACK!");	
	sendImageFromStream();
	
}

static void readDetectionEvent()
{
	if(connfd > 0) {
		uint32_t detectionEventType = readInt(connfd);
		uint32_t timestamp = readInt(connfd);

		syslog(LOG_INFO, "DETECTION EVENT RECEIVED type=%d timestamp=%d" , detectionEventType, timestamp);
	}
}
static guint
subscribe_to_motion(AXEventHandler *event_handler, guint *token)
{
  AXEventKeyValueSet *key_value_set;
  guint subscription;

  key_value_set = ax_event_key_value_set_new();

  GError **error;
  guint window_id = 0;
  gboolean motion = TRUE;
 syslog(LOG_INFO, "Creating key-value set ...");
 ax_event_key_value_set_add_key_values(key_value_set,
         error,
     "topic0", "tns1", "VideoAnalytics", AX_VALUE_TYPE_STRING,
     "topic1", "tnsaxis", "MotionDetection", AX_VALUE_TYPE_STRING,
         EVENT_WINDOW_KEY, NULL, &window_id, AX_VALUE_TYPE_INT,
         EVENT_MOTION_KEY, NULL, &motion, AX_VALUE_TYPE_BOOL,
         NULL);


  /* Time to setup the subscription. Use the "token" input argument as
   * input data to the callback function "subscription callback"
   */
  ax_event_handler_subscribe(event_handler, key_value_set,
        &subscription, (AXSubscriptionCallback)motion_subscription_callback, token,
        NULL);
	 syslog(LOG_INFO, "Subscribed for the event");
  /* The key/value set is no longer needed */
  ax_event_key_value_set_free(key_value_set);

  return subscription;
}

int main(void)
{
	
  GMainLoop *main_loop;
  AXEventHandler *event_handler;
  guint subscription;
  guint token = 1234;
  openlog("secureZoneCamera", LOG_PID, LOG_LOCAL4);

  /**
   * -- socket connection
   */
	syslog(LOG_INFO, "creating socket");
	int listenfd = 0;
	struct sockaddr_in serv_addr;

	char sendBuff[1025];

	//	Create the socket
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&serv_addr, '0', sizeof(serv_addr));
	memset(sendBuff, '0', sizeof(sendBuff));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(5000);

	//	Bind the socket to its settings
	bind(listenfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr));
	syslog(LOG_INFO, "socket bound");
	listen(listenfd, 10);
	connfd = accept(listenfd, (struct sockaddr*) NULL, NULL);
	syslog(LOG_INFO, "Client accepted");
 
	pid_t childPID;
	childPID = fork();
	if(childPID >= 0) 
	{
		if(childPID == 0) 
		{
			while(1) 
			{
				readDetectionEvent();
			}
		} 
		else 
		{
			main_loop = g_main_loop_new(NULL, FALSE);

			event_handler = ax_event_handler_new();
			subscription = subscribe_to_motion(event_handler, &token);
			g_main_loop_run(main_loop);

			ax_event_handler_unsubscribe(event_handler, subscription, NULL);
			ax_event_handler_free(event_handler);
		}
	}
/**
 * -- end socket connection
 */

	return 0;
}

