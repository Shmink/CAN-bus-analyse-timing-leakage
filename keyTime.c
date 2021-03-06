#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>

#include "keyTime.h"

/*
 *	printUsage function prints how to use the program if there aren't enough arguments passed
 *	@param prg - which will be the name of the program shoud it ever change.
 */
void printUsage(char *prg) {
	fprintf(stderr, "Usage: %s [interface] [sending ID] [receive ID] [filename] [samples]\n\n", prg);
	fprintf(stderr, "%s is for analysing timing leakage on ECUs\n\n", prg);
	fprintf(stderr, "Interface: can0 or vcan0 for example.\n");
	fprintf(stderr, "Sending ID: The CAN ID you want to send to.\n");
	fprintf(stderr, "Receive ID: The CAN ID you want to hear from.\n");
	fprintf(stderr, "Filename: What you want to name the output file.\n");
	fprintf(stderr, "Samples: How many attempts would you like to do (Recommended: less than a million if you don't want issues with MS excel or others).\n");
	fprintf(stderr, "\nExample:\n");
	fprintf(stderr, "%s can0 18DA40F1 18DAF140 test 100\n", prg);
}

/*
 *	progMode function puts the ECU into programming progMode
 * 	@param socket - CAN socket used to interface with CAN network
 * 	@param sendID - ID you want to send to
 */
void progMode(int socket, char *sendID, int receiveID) {

	struct canInfoStruct progMode;

	char *progSendID = malloc(25 * sizeof(char));
	strcpy(progSendID, sendID);

	progMode.sendIDinStruct = strcat(progSendID, "#021002"); // put into prog mode
	progMode.receiveIDinStruct = receiveID;
	progMode.sock = socket;
	progMode.beginTime = malloc(sizeof(long));
	progMode.endTime = malloc(sizeof(long));

	sendMsg(&progMode);

	int i = 1;
	while(i) {
		receiveMsg(&progMode);
		if(progMode.cdata[1] == 0x50) {
			i = 0;
		}
	}

	free(progSendID);
	free(progMode.endTime);
	free(progMode.beginTime);
}

/*
 *  seedRequest function asks the ECU for a seed and returns it in the form of int array
 * 	@param socket - CAN socket used to interface with CAN network
 * 	@param sendID - ID you want to send to
 *  @param receiveID - ID you want to receive from
 */
int * seedRequest(int socket, char *sendID, int receiveID) {
	struct canInfoStruct seedRequest;

	char *seedSendID = malloc(25 * sizeof(char));
	strcpy(seedSendID, sendID);

	seedRequest.sendIDinStruct = strcat(seedSendID, "#022701");
	seedRequest.beginTime = malloc(sizeof(long));
	seedRequest.receiveIDinStruct = receiveID;
	seedRequest.sock = socket;
	seedRequest.endTime = malloc(sizeof(long));

	// ##########THREADS###############
	//Create threads as both functions need to be running at the same time.
	pthread_t threadSEND, threadREC; //Don't forget the -pthread flag when compiling with gcc

	// Make threads
	// Running the receive function as it'll be ready and waiting after the frame from send has been sent
	pthread_create(&threadREC, NULL, receiveMsg, &seedRequest);
	pthread_create(&threadSEND, NULL, sendMsg, &seedRequest);

	// Wait for threads to finish
	pthread_join(threadREC, NULL);
	pthread_join(threadSEND, NULL);

	// Saving the seed to be printed later.
	int *framedata = malloc(4 * sizeof(int)); //Only interested in the seed
	framedata[0] = seedRequest.cdata[3];
	framedata[1] = seedRequest.cdata[4];
	framedata[2] = seedRequest.cdata[5];
	framedata[3] = seedRequest.cdata[6];


	free(seedSendID);
	free(seedRequest.beginTime);
	free(seedRequest.endTime);

	return framedata;
}

/*
 * keyTimeTaken function send a key of 0 byte values to ECU.
 * When a response is sent back from the ECU the function should
 * return a how long that gap in time was in the form of a double.
 * 	@param socket - CAN socket used to interface with CAN network
 * 	@param sendID - ID you want to send to
 *  @param receiveID - ID you want to receive from
 */
double keyTimeTaken(int socket, char *sendID, int receiveID) {

	struct canInfoStruct keyTime;

	char *keySendID = malloc(25 * sizeof(char));
	strcpy(keySendID, sendID);

	keyTime.sendIDinStruct = strcat(keySendID, "#06270200000000");
	keyTime.receiveIDinStruct = receiveID;
	keyTime.sock = socket;
	keyTime.beginTime = malloc(sizeof(long));
	keyTime.endTime = malloc(sizeof(long));

	// ##########THREADS###############
	//Create threads as both functions need to be running at the same time.
	pthread_t threadSEND2, threadREC2; //Don't forget the -pthread flag when compiling with gcc

	// Make threads
	// Running the receive function as it'll be ready and waiting after the frame from send has been sent
	pthread_create(&threadREC2, NULL, receiveMsg, &keyTime);
	pthread_create(&threadSEND2, NULL, sendMsg, &keyTime);

	// Wait for threads to finish
	pthread_join(threadREC2, NULL);
	pthread_join(threadSEND2, NULL);

	// ###########TIME################
	// Work out the time difference from when the timer ended to when it started
	long duration = *(keyTime.endTime) - *(keyTime.beginTime);
	// Convert that difference into seconds by dividing by 10^9
	double durSec = ((double)duration)/1e9;

	printf("\n=============" "\x1b[32m" "TIME" "\x1b[0m" "============");
	printf("\n%lf seconds", durSec);


	free(keySendID);
	free(keyTime.beginTime);
	free(keyTime.endTime);

	return durSec;
}

/*
 * resetMode function resets the ECU from a previous programming mode.
 * This is done because if the ECU has too many failed attempts you would get locked out.
 * 	@param socket - CAN socket used to interface with CAN network
 * 	@param sendID - ID you want to send to
 */
void resetMode(int socket, char *sendID, int receiveID) {

	struct canInfoStruct reset;

	char *resetSendID = malloc(25 * sizeof(char));
	strcpy(resetSendID, sendID);

	reset.sendIDinStruct = strcat(resetSendID, "#021101"); // put into reset mode
	reset.receiveIDinStruct = receiveID;
	reset.sock = socket;
	reset.beginTime = malloc(sizeof(long));
	reset.endTime = malloc(sizeof(long));

	sendMsg(&reset);
	sleep(1); 	// sleep just for a bit to make sure the ECU is ready, I don't want to be doing this.

	// TODO: Below is making the ECU freeze up. Needs more testing.
	// printf("\nABOVE RESETMODE LOOP\n");
	// int i = 1;
	// while(i) {
	// 	receiveMsg(&reset);
	// 	if(reset.cdata[1] == 0x51) {
	// 		printf("\nIN RESETMODE LOOP!\n");
	// 		i = 0;
	// 	}
	// }

	free(resetSendID);
	free(reset.beginTime);
	free(reset.endTime);
}

int main(int argc, char *argv[]) {
	// keyTime can0 send receive filename samples
	// ############ARGS#############
	// Check the amount of arguments
	if (argc != 6) {
		printUsage("keyTime");
		return 1;
	}

	char *interfaceName = argv[1];

	char *sendTo = argv[2];
	if(strlen(sendTo) > 8) {
		printf("Send ID was more than 8 digits\n");
		return 0;
	}

	int receiveID;
	sscanf(argv[3], "%x", &receiveID);
	if(strlen(sendTo) > 8) {
		printf("Receive ID was more than 8 digits\n");
		return 0;
	}


	char *filename = argv[4];

	int samples;
	sscanf(argv[5], "%d", &samples);
	// #########END OF ARGS##############

	// Create a socket to use
	int soc = createSocket(interfaceName);

	// Create a file
	FILE *fp;
	filename = strcat(filename, ".csv");
	fp = fopen(filename, "w+");
	// Create headers in the file
	fprintf(fp, "Seed, Time\n");

	int i;
	for(i = 0; i < samples; ++i) {
		printf("\n" "\x1b[32m" "           ATTEMPT " "%d" "\x1b[0m", i+1);

		// Put the ECU in programming mode
		progMode(soc, sendTo, receiveID);

		// Send a seed request and save the seed
		int *seedBytes = seedRequest(soc, sendTo, receiveID);

		// Send a key after the seed has arrived and time how long it takes to hear back from ECU
		double timeTaken = keyTimeTaken(soc, sendTo, receiveID);

		// Output seed and time taken to a CSV
		fprintf(fp, "%02X %02X %02X %02X, %.10f seconds\n", seedBytes[0], seedBytes[1], seedBytes[2], seedBytes[3], timeTaken);

		// Reset the ECU as enough failed keys will lock you out for ~5 seconds.
		if((samples%2 == 0) && (samples > 0))
			resetMode(soc, sendTo, receiveID);

		free(seedBytes);
	}

	// Close file
	fclose(fp);
	printf("\n\n%s saved\n", filename);

	return 0;
}
