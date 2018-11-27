#include "io.h"
#include "system.h"
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "altera_avalon_pio_regs.h"
#include "altera_avalon_uart_regs.h"
#include "altera_avalon_jtag_uart_regs.h"

#define TRUE 1
#define FALSE 0
#define MAX_OPTIONS_NUMBER 5
#define RETURN_HOME 0x01
#define CLEAR_DISPLAY 0x02
#define MQTT_MAX_FIXED_HEADER_SIZE 3

#define WIFI_MODE "AT+CWMODE=3\r\n"
#define WIFI_CONNECT "AT+CWJAP=\"WLessLEDS\", \"HelloWorldMP31\"\r\n"

#define TCP_CONNECT "AT+CIPSTART=\"TCP\",\"192.168.1.103\",1883\r\n"
#define TCP_DISCONNECT "AT+CIPCLOSE\r\n"

#define MQTT_CONNECT_SIZE "AT+CIPSEND=20\r\n"
#define MQTT_CONNECT "\x10\x12\x00\x04\aMQTT\x04\x02\x00\x14\x00\x06\atest/test\r\n"

#define MESSAGE_SIZE "AT+CIPSEND=27\r\n"

int choosed = FALSE;

/* LCD CONTROL */
int nextOption (int currentOption);
int previousOption (int currentOption);
void showOption (int currentOption);
void enterOption (int currentOption);
void exitOption (int currentOption);
void showLed(int currentOption);
void writeWord(char word[]);
void hideAllLeds();
void initializeDisplay();

/* ESP CONTROL */

void startConnection(int code);
void connectToRouter();
void createClient();
void sendCommand();
void connectMQTT(int size, char* command, char* packet);
void sendData(int code);

/* UTIL */
void printJTAG(char* data);
void printInt(int data);

//Initialize the program
int main()
{
	unsigned int buttonUp = 0, buttonDown = 0, buttonExit = 0, buttonEnter = 0 ;
	int currentOption = 0;

	initializeDisplay();
	usleep(2000);
	printf("Inicializando");

	// Start a connection with router and create a TCP packet.
	alt_putstr("AT+RST\r\n");
	printJTAG('$');
	usleep(1000000);
	printJTAG('@');
	sendCommand("AT+CWMODE=3\r\n");
	connectToRouter();
	createClient();

	// Wait for an user action
	while (TRUE) {
		buttonUp = IORD_ALTERA_AVALON_PIO_DATA(BUTTON_UP_BASE);
		buttonDown = IORD_ALTERA_AVALON_PIO_DATA(BUTTON_DOWN_BASE);
		buttonEnter = IORD_ALTERA_AVALON_PIO_DATA(BUTTON_ENTER_BASE);
		buttonExit = IORD_ALTERA_AVALON_PIO_DATA(BUTTON_EXIT_BASE);

		if (buttonUp && !choosed) {
			currentOption = previousOption(currentOption);
			showOption(currentOption);
			usleep(100000);
		} else if (buttonDown && !choosed) {
			currentOption = nextOption(currentOption);
			showOption(currentOption);
			usleep(100000);
		} else if (buttonEnter && !choosed) {
			enterOption(currentOption);
			startConnection(currentOption); // Calls ESP Connection
			usleep(100000);
		} else if (buttonExit) {
			exitOption(currentOption);
			usleep(100000);
		}

		usleep(500);
	}
	return 0;
}

/******************************
* MQTT and ESP8266 Functions **
*******************************/

// Send an AT Command to ESP8266 and wait for OK
void sendCommand (char* command) {
	char receive;
	alt_putstr(command);

	while (1) {

		if(IORD_ALTERA_AVALON_UART_STATUS(UART_MAIN_BASE) & (1<<7)) {
			receive = IORD_ALTERA_AVALON_UART_RXDATA(UART_MAIN_BASE);

			printJTAG(receive);

			if (receive == 'K') {
				return;
			}
		}
	}
}

// Manage the MQTT connection
void startConnection (int code) {
	sendData(code);
	return;
}

// Connect to Router
void connectToRouter () {
	sendCommand("AT+CWJAP=\"WLessLEDS\",\"HelloWorldMP31\"\r\n");
}

// Create a TCP Packet and send a "Connect" message to Broker
void createClient () {
	sendCommand("AT+CIPSTART=\"TCP\",\"192.168.1.103\",1883\r\n");
	char packetConnect[] = {
		0x10, 0x12, 0x00, 0x04, 0x4D, 0x51, 0x54,
		0x54, 0x04, 0x02, 0x00, 0x14, 0x00, 0x05,
		0x4E, 0x69, 0x6F, 0x73, 0x20, 0x32
	};
	connectMQTT(sizeof(packetConnect), MQTT_CONNECT_SIZE, packetConnect);
}

// Send a MQTT message and wait for OK from ESP8266
void connectMQTT (int size, char* command, char* packet) {

	usleep(1000000);
	char receive;

	alt_putstr(command);

	while (1) {
		if(IORD_ALTERA_AVALON_UART_STATUS(UART_MAIN_BASE) & (1<<7)) {
			receive = IORD_ALTERA_AVALON_UART_RXDATA(UART_MAIN_BASE);
			printJTAG(receive);
			
			int i=0;

			if (receive == '>') {
				for (i = 0; i < size; i++) {
					while(!(IORD_ALTERA_AVALON_UART_STATUS(UART_MAIN_BASE) & (1<<6)));
					IOWR_ALTERA_AVALON_UART_TXDATA(UART_MAIN_BASE, packet[i]);
				}
				while(!(IORD_ALTERA_AVALON_UART_STATUS(UART_MAIN_BASE) & (1<<6)));
				IOWR_ALTERA_AVALON_UART_TXDATA(UART_MAIN_BASE, '\r');

				while(!(IORD_ALTERA_AVALON_UART_STATUS(UART_MAIN_BASE) & (1<<6)));
				IOWR_ALTERA_AVALON_UART_TXDATA(UART_MAIN_BASE, '\n');

				while (1) {
					if(IORD_ALTERA_AVALON_UART_STATUS(UART_MAIN_BASE) & (1<<7)) {
						receive = IORD_ALTERA_AVALON_UART_RXDATA(UART_MAIN_BASE);

						printJTAG(receive);

						if(receive == 'K') {
							return;
						}
					}
				}
			}
		}
	}
}

// Choose a packet to be sent to Broker
void sendData (int code) {

	char packetMessage1[] = {
		0x30, 0x19, 0x00, 0x0B, 0x74, 0x65, 0x73, 0x74, 0x65,
		0x2f, 0x74, 0x65, 0x73, 0x74, 0x65, 0x53, 0x45, 0x4c,
		0x45, 0x43, 0x49, 0x4f, 0x4e, 0x4f, 0x55, 0x20, 0x31
	};

	char packetMessage2[] = {
		0x30, 0x19, 0x00, 0x0B, 0x74, 0x65, 0x73, 0x74, 0x65,
		0x2f, 0x74, 0x65, 0x73, 0x74, 0x65, 0x53, 0x45, 0x4c,
		0x45, 0x43, 0x49, 0x4f, 0x4e, 0x4f, 0x55, 0x20, 0x32
	};

	char packetMessage3[] = {
		0x30, 0x19, 0x00, 0x0B, 0x74, 0x65, 0x73, 0x74, 0x65,
		0x2f, 0x74, 0x65, 0x73, 0x74, 0x65, 0x53, 0x45, 0x4c,
		0x45, 0x43, 0x49, 0x4f, 0x4e, 0x4f, 0x55, 0x20, 0x33
	};

	char packetMessage4[] = {
		0x30, 0x19, 0x00, 0x0B, 0x74, 0x65, 0x73, 0x74, 0x65,
		0x2f, 0x74, 0x65, 0x73, 0x74, 0x65, 0x53, 0x45, 0x4c,
		0x45, 0x43, 0x49, 0x4f, 0x4e, 0x4f, 0x55, 0x20, 0x34
	};

	char packetMessage5[] = {
		0x30, 0x19, 0x00, 0x0B, 0x74, 0x65, 0x73, 0x74, 0x65,
		0x2f, 0x74, 0x65, 0x73, 0x74, 0x65, 0x53, 0x45, 0x4c,
		0x45, 0x43, 0x49, 0x4f, 0x4e, 0x4f, 0x55, 0x20, 0x35
	};

	usleep(1000000);
	if (code == 0) {
		connectMQTT(sizeof(packetMessage1), MESSAGE_SIZE, packetMessage1);
	} else if (code == 1) {
		connectMQTT(sizeof(packetMessage2), MESSAGE_SIZE, packetMessage2);
	} else if (code == 2) {
		connectMQTT(sizeof(packetMessage3), MESSAGE_SIZE, packetMessage3);
	}	else if (code == 3) {
		connectMQTT(sizeof(packetMessage4), MESSAGE_SIZE, packetMessage4);
	} else if (code == 4) {
		connectMQTT(sizeof(packetMessage5), MESSAGE_SIZE, packetMessage5);
	}
}


/**********************
* LCD FUNCTIONS		***
***********************/


// Initialize the LCD display
void initializeDisplay () {
	usleep(15000);
	IOWR(LCD_OUTPUT_BASE, 0, 0x30);
	usleep(4100);
	IOWR(LCD_OUTPUT_BASE, 0, 0x30);
	usleep(100);
	IOWR(LCD_OUTPUT_BASE, 0, 0x30);
	usleep(5000);
	IOWR(LCD_OUTPUT_BASE, 0, 0x39);
	usleep(100);
	IOWR(LCD_OUTPUT_BASE, 0, 0x14);
	usleep(100);
	IOWR(LCD_OUTPUT_BASE, 0, 0x56);
	usleep(100);
	IOWR(LCD_OUTPUT_BASE, 0, 0x6D);
	usleep(100);
	IOWR(LCD_OUTPUT_BASE, 0, 0x70);
	usleep(2000);
	IOWR(LCD_OUTPUT_BASE, 0, 0x0C);
	usleep(2000);
	IOWR(LCD_OUTPUT_BASE, 0, 0x06);
	usleep(2000);
	IOWR(LCD_OUTPUT_BASE, 0, 0x01);
	usleep(2000);
	hideAllLeds();
	showOption(0);
}

// Choose the next menu option
int nextOption (int currentOption) {
	int option = 0;

	if (currentOption == MAX_OPTIONS_NUMBER - 1) {
		option =  0;
	} else {
		option =  currentOption + 1;
	}
	return option;
}

// Choose the previous menu option
int previousOption (int currentOption) {
	int option = 0;

	if (currentOption == 0) {
		option =  MAX_OPTIONS_NUMBER - 1;
	} else {
		option =  currentOption - 1;
	}
	return option;
}

// Choose the show the current menu option
void showOption (int currentOption) {
	char *a[5];
	a[0] = "Opcao 1";
	a[1] = "Opcao 2";
	a[2] = "Opcao 3";
	a[3] = "Opcao 4";
	a[4] = "Opcao 5";

	writeWord(a[currentOption]);
	usleep(500);
}

// Choose a option in menu
void enterOption (int currentOption) {
	char *a[5];
	a[0] = "Escolheu 1";
	a[1] = "Escolheu 2";
	a[2] = "Escolheu 3";
	a[3] = "Escolheu 4";
	a[4] = "Escolheu 5";
	choosed = TRUE;

	writeWord(a[currentOption]);
	showLed(currentOption);
	usleep(500);
}

// Write a word on LCD
void writeWord(char word[]) {
	IOWR(LCD_OUTPUT_BASE, 0, CLEAR_DISPLAY);
	usleep(2000);
	IOWR(LCD_OUTPUT_BASE, 0, RETURN_HOME);
	usleep(2000);

	int i = 0;

	for(i=0; i < strlen(word); i++){
		IOWR(LCD_OUTPUT_BASE, 2, word[i]);
		usleep(1000);
	}
	usleep(500);
}

// Choose a LED color
void showLed (int currentOption) {
	hideAllLeds();

	switch(currentOption) {
	case 0: //RED
		IOWR(LED_R_BASE, 0, 1);
		IOWR(LED_G_BASE, 0, 0);
		IOWR(LED_B_BASE, 0, 0);
		usleep(100000);
		break;
	case 1: // GREEN
		IOWR(LED_R_BASE, 0, 0);
		IOWR(LED_G_BASE, 0, 1);
		IOWR(LED_B_BASE, 0, 0);
		usleep(100000);
		break;
	case 2: // BLUE
		IOWR(LED_R_BASE, 0, 0);
		IOWR(LED_G_BASE, 0, 0);
		IOWR(LED_B_BASE, 0, 1);
		usleep(100000);
		break;
	case 3: // MAGENTA
		IOWR(LED_R_BASE, 0, 1);
		IOWR(LED_G_BASE, 0, 0);
		IOWR(LED_B_BASE, 0, 1);
		usleep(100000);
		break;
	case 4: // YELLOW
		IOWR(LED_R_BASE, 0, 1);
		IOWR(LED_G_BASE, 0, 1);
		IOWR(LED_B_BASE, 0, 0);
		usleep(100000);
		break;
	}
}

// "Turn off" all leds. Turn on the white LED
void hideAllLeds () {
	IOWR(LED_R_BASE, 0, 1);
	IOWR(LED_G_BASE, 0, 1);
	IOWR(LED_B_BASE, 0, 1);
}

// Exit from a choosed menu option
void exitOption (int currentOption) {
	hideAllLeds();
	showOption(currentOption);
	choosed = FALSE;
	usleep(500);
}

/*
* Util functions
*/
// Print a char on console
void printJTAG (char* data) {
	IOWR_ALTERA_AVALON_JTAG_UART_DATA(JTAG_UART_0_BASE, data);
}

// Print a integer on console
void printInt (int data) {
	char d[2];
	sprintf(d, "%d", data);
	int i = 0;
	int size = strlen(d);

	for (i = 0; i < size; i++) {
		IOWR_ALTERA_AVALON_JTAG_UART_DATA(JTAG_UART_0_BASE, d[i]);
	}

}