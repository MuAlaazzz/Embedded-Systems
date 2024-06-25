#include <LiquidCrystal.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <SoftwareSerial.h>
#define buad 115200
SoftwareSerial ESP01(11, 10);
bool TempFlagWebPage = false;
char* LCD_INFO_PIR;
char* LCD_INFO_Smoke;
bool PIR_Flag_Detection = false;
bool Smoke_Flag_Detecton = false;
LiquidCrystal lcd(8, 9, 4, 5, 6, 7); // Initialize the library with the numbers of the interface pins


// IMPORTANT : ATMEGA328p has frequency of 16MHz resulting in clock period = 0.0625µs 



void Timer0_Delay1ms() {
  //you cannot configure CTC mode using only TCCR0B because it does not contain WGM01 so we need TCCR0A.
  TCCR0A |= (1 << WGM01);  // CTC mode
  TCCR0B |= 0x03;          //64 prescale
  // 16/64 = 0.25 and 1/0.25 = 4µs and the delay is  1ms so OCR0A should contain value of ( 1 * 10^3 / 4 ) = 250 clocks 
  OCR0A = 250; //count from 0 -> 250
  while ((TIFR0 & (1 << OCF0A)) == 0);
  TCCR0B = 0; //(reset control register to no clock source)
  TIFR0 = (1 << OCF0A); //timer has overflowed (compare match occured)
}

bool Timer0_Delay_ms(int ms) {
  for (int i = 0; i < ms; i++) {
    Timer0_Delay1ms();
  }
  return true;
}


void Timer1_Init_Interrupt() {
  //Set up Timer 1 to generate an interrupt every 2 seconds
  cli();       // Disable interrupt
  TCCR1A = 0;  // (reset everything "normal mode")
  TCCR1B = 0;  //(reset everyting "no clock")
  // 16/1024 = 0.015625 and 1/0.015625 = 64 and the delay is 2s so ( 2 * 10^6 / 64) = 31250 = 0x7A12 clocks 
  OCR1AH = 0x7A;
  OCR1AL = 0x12;                   
  TCCR1B |= (1 << WGM12);               // Set Timer 1 to CTC mode
  TCCR1B |= (1 << CS12) | (1 << CS10);  // Set Timer 1 prescaler to 1024
  TIMSK1 |= (1 << OCIE1A);              // Enable Timer 1 interrupt (TIMSK1 used because it is not a external interrupt and also here OCIE1A instead of TOIE1 because of CTC mode)
  sei();                                // Enable interrupts
}

ISR(TIMER1_COMPA_vect)  // when the timer overflows and the compare match occurs do -> ...
{
  // the timer is overflowed every two seconds so the readings will be printed on the LCD every 2's
  LCD_Output(PIR_READ_LCD(), Analog_Read(0), Smoke_READ_LCD()); // (print on the LCD the read of the motion sensor, temperature sensor and smoke sensor)
  PIR_Flag_Detection = false; // reset motion sensor flag to no motion 
}


char* PIR_READ_LCD() {
  if (PIR_Flag_Detection) {
    LCD_INFO_PIR = "motion detected";
  } else {
    LCD_INFO_PIR = "motion not detected";
  }
  return LCD_INFO_PIR;
}

char* Smoke_READ_LCD() {
  if (Smoke_Flag_Detecton) {
    LCD_INFO_Smoke = "Smoke";
  } else {
    LCD_INFO_Smoke = "NO Smoke";
  }
  return LCD_INFO_Smoke;
}

ISR(INT0_vect) {
  PIR_Flag_Detection = true; //INT0 is used to make interrupts using external hardware so when there is any motion the interrupt will happen and the flag will be set to true 
}
ISR(INT1_vect) {
  if (Smoke_Flag_Detecton) { 
    Smoke_Flag_Detecton = false;
    EICRA |= (1 << ISC11);  //Falling Edge Trigger
    EICRA &= 0xFB;          //Clear ISC10 (Setting ISC10 to 0)
  } else {
    Smoke_Flag_Detecton = true; 
    EICRA |= (1 << ISC11) | (1 << ISC10); //Rising Edge Trigger
  }
}

int Analog_Read(uint8_t pin_num) {
  // FOR TEMPERATURE SENSOR
  ADMUX |= (1 << REFS0); //Vref is same as VCC 
  ADCSRA |= (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0) | (1 << ADEN); //enable ADC and adjust clock Division Factor/128 to improve accuracy and improve conversion stability  
  ADMUX |= pin_num; //choosing channel based on the pin_num 
  ADCSRA |= (1 << ADSC); //start conversion
  while (ADCSRA & (1 << ADIF) == 0); //wait for conversion to finish 
  float data = ADC;  // ADCH:ADCL
  //To convert this digital value back to voltage, you need to scale it based on the reference voltage (Vref) and the resolution of the ADC.
  data = (data * 5000 / 1024) / 10; //5000 is the Vref in mv and 1024 is the number of steps(2^10) and 10 mv is the production of the sensor for each degree of temperature 
  //converts the raw ADC value (data) obtained from the temperature sensor into a temperature value
  return (int)data;
}

void INT0_PIR_init(void) {
  // Configure PD2 (INT0 pin) as input with pull-up resistor enabled
  DDRD &= ~(1 << PD2); //putting zero for the pin (zero -> input , one -> output)
  PORTD |= (1 << PD2); //Pull-Up (putting 1 for the pin to use the the internal pull up resistor) 
  //AVR microcontrollers, including the ATmega series, typically have internal pull-up resistors that can be enabled via software
  //To enable the internal pull-up resistors on the ATmega microcontroller, you need to configure the corresponding pin as an input and then set the pin's corresponding bit in the PORT register to high
  EICRA |= (1 << ISC01) | (1 << ISC00);   //Configure INT0 to trigger on rising edge (like MCUCR register in lecture) 
  EIMSK |= (1 << INT0);  //Enable INT0 interrupt 
}

void INT1_Smoke_init(void) {
  // Configure PD3 (INT1 pin) as input with pull-up resistor enabled
  DDRD &= ~(1 << PD3); //putting zero for the pin (zero -> input , one -> output)
  PORTD |= (1 << PD3); //Pull-Up (putting 1 for the pin to use the the internal pull up resistor) 
  EICRA |= (1 << ISC11);  //Configure INT1 to trigger on falling edge 
  EIMSK |= (1 << INT1);  //Enable INT1 interrupt
}

void LCD_Output(char* pir, int dergree, char* smoke) {
  lcd.begin(16, 2); 
  //Initializes the LCD with 16 columns and 2 rows
  lcd.setCursor(0, 0);
  lcd.print("TEMP");
  //Sets the cursor to the first column of the first row (0,0) and prints the string "TEMP".
  lcd.setCursor(5, 0);
  lcd.print(dergree);
  //Sets the cursor to the sixth column of the first row (5,0) and prints the temperature value dergree
  lcd.setCursor(8, 0);
  lcd.print("/");
  //Sets the cursor to the ninth column of the first row (8,0) and prints a "/".
  lcd.setCursor(9, 0);
  //Sets the cursor to the tenth column of the first row (9,0).
  for (int i = 0; i < strlen(smoke); i++) {
    //iterate through each character and print it
    lcd.print(smoke[i]);
  }
  lcd.setCursor(0, 1);
  for (int i = 0; i < strlen(pir); i++) {
    //iterate through each character and print it
    lcd.print(pir[i]);
  }
}

String SendComandESP(String command, int times) { 
  bool time = false;
  String response = "";
  //print -> bb3t lel ESP  
  ESP01.print(command); //Sends the command to the ESP module via the ESP01 object
  while (!time) { //until time becomes true, loop 
    while (ESP01.available()) { //Enters a nested loop that continues as long as there are bytes available to read from the ESP module.
      //read -> b2ra menha 
      char c = ESP01.read();
      response += c; //Reads each available character from the ESP module and appends it to the response string
    }
    //Serial.print(response); 
    time = Timer0_Delay_ms(times);
  }                                                         
  return response;
}

void StartESP() {
  // SendComandESP("AT+RST\r\n",1000); //restart
  //SendComandESP("AT+RESTORE\r\n",10000); //reset settings 
  SendComandESP("AT+UART_CUR=115200,8,1,0,0\r\n", 1000); //Purpose: Configuring the UART communication parameters such as baud rate, data bits, stop bits, etc.
  SendComandESP("AT+CWJAP=\"Mohamed1&\",\"Medo omega 6767$$\"\r\n", 5000); //Purpose: Establishing a connection to the specified Wi-Fi network.
  SendComandESP("AT+CWMODE=1\r\n", 2000); //Purpose: Setting the mode of the ESP module to operate as a Wi-Fi client (station) rather than an access point.
  SendComandESP("AT+CIFSR\r\n", 2000); //Purpose: Obtaining the IP address assigned to the ESP module after it has connected to the Wi-Fi network.
  SendComandESP("AT+CIPMUX=1\r\n", 2000); //Purpose: Configuring the ESP module to allow multiple TCP connections simultaneously.
  SendComandESP("AT+CIPSERVER=1,80\r\n", 2000); // Setting up the ESP module to listen for incoming TCP connections on port 80, typically used for HTTP communication.
}

void Wifi_Send() {
  if (ESP01.available()) {
    //This line searches for the occurrence of the "+IPD," string in the incoming data. This string indicates the start of incoming data over the network connection.
    if (ESP01.find("+IPD,")) {
      Timer0_Delay_ms(500);
      ESP01.read(); //-48 converts the ASCII character representing the connection ID to its numeric value.
      int connectionId = ESP01.read() - 48;
      // Read the request
      String request = "";
      while (ESP01.available()) {
        char c = ESP01.read();
        request += c;
        //The request is typically an HTTP GET request sent by a client, containing instructions or commands.
      }
      Serial.println(request);
      // Check if the request is for LED control
      if (request.indexOf("led1_on") != -1) {
        PORTB |= (1 << PC1);
      } else if (request.indexOf("led1_off") != -1) {
        PORTB &= ~(1 << PC1);
      } else if (request.indexOf("led2_on") != -1) {
        PORTB |= (1 << PB5);
      } else if (request.indexOf("led2_off") != -1) {
        PORTB &= ~(1 << PB5);
      }
      if (Analog_Read(0) > 30) {
        TempFlagWebPage = true;
      } else {
        TempFlagWebPage = false;
      }
      String response = "HTTP/1.1 200 OK\r\n";
      response += "Content-Type: text/html\r\n\r\n"; //btghz el response
      response += "<html><head><title>LED Control</title></head><body><h1>Control the LEDs</h1><button onclick=\"sendCommand('led1_on')\">LED 1 On</button><button onclick=\"sendCommand('led1_off')\">LED 1 Off</button><br><br><button onclick=\"sendCommand('led2_on')\">LED 2 On</button><button onclick=\"sendCommand('led2_off')\">LED 2 Off</button><script>function sendCommand(command) {var xhttp = new XMLHttpRequest();xhttp.open('GET', '/'+command, true);xhttp.send();}</script></body></html>";
       if (TempFlagWebPage) {
        response += "<p>Temperature Warning!</p>"; //check if temperature is above 30C and send a warning is so 
      }
      if (Smoke_Flag_Detecton) {
        response += "<p>Smoke Warning!</p>";  // Check smoke flag and add warning message if necessary
      }
      String cipSend = "AT+CIPSEND=";
      cipSend += connectionId;
      cipSend += ",";
      cipSend += response.length();
      cipSend += "\r\n"; 
      SendComandESP(cipSend, 500); //constructs a command (cipSend) to instruct the ESP module to start sending data to a specific connection ID.
      ESP01.print(response); //This line sends the HTTP response to the client via the ESP module
      Timer0_Delay_ms(500);
      String closeCommand = "AT+CIPCLOSE=";
      closeCommand += connectionId;
      closeCommand += "\r\n";
      SendComandESP(closeCommand, 1000); //constructs a command (closeCommand) to close the TCP/IP connection with the client.
    }
  }
}
void setup() {
  DDRB |= (1 << PB4) | (1 << PB5); // leds output
  Timer1_Init_Interrupt();  // Initialize LCD event 
  Serial.begin(buad);
  ESP01.begin(buad);
  Timer0_Delay_ms(1000); //delay one second before starting the ESP
  StartESP();
  INT0_PIR_init();    // Initialize INT0 (PIR)
  INT1_Smoke_init();  // Initialize INT1 (Smoke)
}

void loop() {
  Wifi_Send();
}
