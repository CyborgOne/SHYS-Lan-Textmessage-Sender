/* 
 homecontrol incl. Intertechno BT-Switch Support - v1.2
 
 Arduino Sketch für homecontrol 
 Copyright (c) 2016 Daniel Scheidler All right reserved.
 
 homecontrol ist Freie Software: Sie können es unter den Bedingungen
 der GNU General Public License, wie von der Free Software Foundation,
 Version 3 der Lizenz oder (nach Ihrer Option) jeder späteren
 veröffentlichten Version, weiterverbreiten und/oder modifizieren.
 
 homecontrol wird in der Hoffnung, dass es nÃ¼tzlich sein wird, aber
 OHNE JEDE GEWÄHRLEISTUNG, bereitgestellt; sogar ohne die implizite
 GewÃ¤hrleistung der MARKTFÄHIGKEIT oder EIGNUNG FÜR EINEN BESTIMMTEN ZWECK.
 Siehe die GNU General Public License für weitere Details.
 
 Sie sollten eine Kopie der GNU General Public License zusammen mit diesem
 Programm erhalten haben. Wenn nicht, siehe <http://www.gnu.org/licenses/>.
 */


#include <SPI.h>
#include <Ethernet.h>
#include <SoftwareSerial.h>
#include <RF24.h> 
#include <DigitalIO.h> // https://github.com/greiman/DigitalIO   

// Patch file RF24_config.h in RF24 Library to enable softspi to use NRF with Ethernetshield
// - uncomment (remove //) #define SOFTSPI
// - change pin numbers as below:
//      const uint8_t SOFT_SPI_MISO_PIN = 15;
//      const uint8_t SOFT_SPI_MOSI_PIN = 14;
//      const uint8_t SOFT_SPI_SCK_PIN = 16;
// - Use Pins A0-A2 for Mosi(A0), Miso(A1) and SCK(A2) instead of 11-13


 
// ---------------------------------------------------------------
// --                      START CONFIG                         --
// ---------------------------------------------------------------
boolean serialOut = true;

// ---------------------------------------------------------------
// --                       END CONFIG                          --
// ---------------------------------------------------------------

// ClientNummer 2 wird von der Uhr als anzuzeigende Nachricht ausgewertet
byte ClientNummer = 2; // Mögliche Werte: 1-6
 
static const uint64_t pipes[6] = {0xF0F0F0F0E1LL, 0xF0F0F0F0D2LL, 0xF0F0F0F0C3LL, 0xF0F0F0F0B4LL, 0xF0F0F0F0A5LL, 0xF0F0F0F096LL};

char signalValue[32] = "";

RF24 radio(9,10);


// Netzwerkdienste
EthernetServer HttpServer(80); 
EthernetClient interfaceClient;


// Webseiten/Parameter
const int  MAX_BUFFER_LEN           = 50; // max characters in page name/parameter 
char       buffer[MAX_BUFFER_LEN+1]; // additional character for terminating null
 
#if defined(__SAM3X8E__)
    #undef __FlashStringHelper::F(string_literal)
    #define F(string_literal) string_literal
#endif

const __FlashStringHelper * htmlHeader;
const __FlashStringHelper * htmlHead;
const __FlashStringHelper * htmlFooter;

// ------------------ Reset stuff --------------------------
void(* resetFunc) (void) = 0;
unsigned long resetMillis;
boolean resetSytem = false;
// --------------- END - Reset stuff -----------------------



/**
 * SETUP
 */
void setup() {
  unsigned char mac[]  = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED  };
  unsigned char ip[]   = { 192, 168, 1, 22 };
  unsigned char dns[]  = { 192, 168, 1, 1  };
  unsigned char gate[] = { 192, 168, 1, 1  };
  unsigned char mask[] = { 255, 255, 255, 0  };
  
  // Serial initialisieren
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }
  Serial.println(F("SmartHome yourself - Textmessage"));
  Serial.println();
  
  // Netzwerk initialisieren
  Ethernet.begin(mac, ip, dns, gate, mask);
  HttpServer.begin();

  Serial.print( F("IP: ") );
  Serial.println(Ethernet.localIP());

  initStrings();

  radio.begin();
  delay(20);

  radio.setChannel(1);                // Funkkanal - Mögliche Werte: 0 - 127
  radio.setAutoAck(0);
  radio.setRetries(15,15);    
  radio.setPALevel(RF24_PA_HIGH);     // Sendestärke darf die gesetzlichen Vorgaben des jeweiligen Landes nicht überschreiten! 
                                      // RF24_PA_MIN=-18dBm, RF24_PA_LOW=-12dBm, RF24_PA_MED=-6dBM, and RF24_PA_HIGH=0dBm
  radio.setDataRate(RF24_1MBPS);                                  

  radio.openWritingPipe(pipes[ClientNummer-1]);
  radio.openReadingPipe(1, pipes[0]); 
 
  radio.startListening();
  delay(20);  
}


/**
 * Standard Loop Methode
 */
void loop() {
   EthernetClient client = HttpServer.available();
   
  if (client) {
    while (client.connected()) {
      if(client.available()){        
        if(serialOut){
          Serial.println(F("Website anzeigen"));
        }
        showWebsite(client);
         
        delay(100);
        client.stop();
      }
    }
  }
  
  delay(100);
  strcpy(signalValue,"");
}




void sendMessage(){
  radio.stopListening(); 
  delay(100);
  radio.write(&signalValue, sizeof(signalValue));
  delay(100);  
  radio.write(&signalValue, sizeof(signalValue));
  delay(100);     
  radio.write(&signalValue, sizeof(signalValue));
  delay(100);      
    
  Serial.print("Output: ");
  Serial.println(signalValue);
  
  radio.txStandBy();          // Need to drop out of TX mode every 4ms if sending a steady stream of multicast data
  delayMicroseconds(130);     // This gives the PLL time to sync back up   

  radio.startListening();
}



// ---------------------------------------
//     Webserver Hilfsmethoden
// ---------------------------------------

/**
 *  URL auswerten und entsprechende Seite aufrufen
 */
void showWebsite(EthernetClient client){
  char * HttpFrame =  readFromClient(client);
  
 // delay(200);
  boolean pageFound = false;
  
  char *ptr = strstr(HttpFrame, "favicon.ico");
  if(ptr){
    pageFound = true;
  }
  ptr = strstr(HttpFrame, "index.html");
  if (!pageFound && ptr) {
    runIndexWebpage(client);
    pageFound = true;
  } 
  ptr = strstr(HttpFrame, "rawCmd");
  if(!pageFound && ptr){
    runRawCmdWebpage(client, HttpFrame);
    pageFound = true;
  } 

  delay(200);

  ptr=NULL;
  HttpFrame=NULL;

 if(!pageFound){
    runIndexWebpage(client);
  }
  delay(20);
}



// ---------------------------------------
//     Webseiten
// ---------------------------------------

/**
 * Startseite anzeigen
 */
void  runIndexWebpage(EthernetClient client){
  showHead(client);

  client.print(F("<h4>Navigation</h4><br/>"
    "<a href='/rawCmd'>Manuelle Schaltung</a><br>"));

  showFooter(client);
}


/**
 * rawCmd anzeigen
 */
void  runRawCmdWebpage(EthernetClient client, char* HttpFrame){
  if (strlen(signalValue)!=0 ) {
    postRawCmd(client, signalValue);
    return;
  }

  showHead(client);
  
  client.println(F(  "<h4>Manuelle Schaltung</h4><br/>"
                     "<form action='/rawCmd'>"));

  client.println(F( "<b>Nachricht: </b>" 
                    "<input type='text' name='text' size='32' maxlength='32'>"));
  client.println(F( "<input type='submit' value='Abschicken'/>"
                    "</form>"));

  showFooter(client);
}


void postRawCmd(EthernetClient client, char* irCode){
  showHead(client);
    
  client.println(F( "<h4> Nachricht senden</h4><br/>" ));
  client.print(F( "Nachricht: " ));
  client.println(irCode);
  sendMessage();
  
  showFooter(client);
}





// ---------------------------------------
//     HTML-Hilfsmethoden
// ---------------------------------------

void showHead(EthernetClient client){
  client.println(htmlHeader);
  client.print("IP: ");
  client.println(Ethernet.localIP());
  client.println(htmlHead);
}


void showFooter(EthernetClient client){
  client.println(F("<div  style=\"position: absolute;left: 30px; bottom: 40px; text-align:left;horizontal-align:left;\" width=200>"));
 
  client.println(F("</div>"));
  client.print(htmlFooter);
}


void initStrings(){
  htmlHeader = F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n");
  
  htmlHead = F("<html><head>"
    "<title>HomeControl</title>"
    "<style type=\"text/css\">"
    "body{font-family:sans-serif}"
    "*{font-size:14pt}"
    "a{color:#abfb9c;}"
    "</style>"
    "</head><body text=\"white\" bgcolor=\"#494949\">"
    "<center>"
    "<hr><h2>SmartHome yourself - Textnachricht NRF</h2><hr>") ;
    
    htmlFooter = F( "</center>"
    "<a  style=\"position: absolute;left: 30px; bottom: 20px; \"  href=\"/\">Zurueck zum Hauptmenue;</a>"
    "</body></html>");
    
}


// ---------------------------------------
//     Ethernet - Hilfsmethoden
// ---------------------------------------
/**
 * Zum auswerten der URL des Ã¼bergebenen Clients
 * (implementiert um angeforderte URL am lokalen Webserver zu parsen)
 */
char* readFromClient(EthernetClient client){
  char paramName[20];
  char paramValue[50];
  char pageName[20];
  
  if (client) {
  
    while (client.connected()) {
  
      if (client.available()) {
        memset(buffer,0, sizeof(buffer)); // clear the buffer

        client.find("/");
        
        if(byte bytesReceived = client.readBytesUntil(' ', buffer, sizeof(buffer))){ 
          buffer[bytesReceived] = '\0';

          if(serialOut){
            Serial.print(F("URL: "));
            Serial.println(buffer);
          }
          
          if(strcmp(buffer, "favicon.ico\0")){
            char* paramsTmp = strtok(buffer, " ?=&/\r\n");
            int cnt = 0;
            
            while (paramsTmp) {
            
              switch (cnt) {
                case 0:
                  strcpy(pageName, paramsTmp);
                  if(serialOut){
                    Serial.print(F("Domain: "));
                    Serial.println(buffer);
                  }
                  break;
                case 1:
                  strcpy(paramName, paramsTmp);
                
                  if(serialOut){
                    Serial.print(F("Parameter: "));
                    Serial.print(paramName);
                  }
                  break;
                case 2:
                  strcpy(paramValue, paramsTmp);
                  if(serialOut){
                    Serial.print(F(" = "));
                    Serial.println(paramValue);
                  }
                  pruefeURLParameter(paramName, paramValue);
                  break;
              }
              
              paramsTmp = strtok(NULL, " ?=&/\r\n");
              cnt=cnt==0?1:cnt==1?2:1;
            }
          }
        }
      }// end if Client available
      break;
    }// end while Client connected
  } 

  return buffer;
}


void pruefeURLParameter(char* tmpName, char* value){
  if(strcmp(tmpName, "text")==0 && strcmp(value, "")!=0){
    strcpy(signalValue, value);
    
    if(serialOut){
      Serial.print(F("Text: "));
      Serial.println(signalValue);    
    }
  }
}



// ---------------------------------------
//     Allgemeine Hilfsmethoden
// ---------------------------------------
char* int2bin(unsigned int x){
  static char buffer[6];
  for (int i=0; i<5; i++) buffer[4-i] = '0' + ((x & (1 << i)) > 0);
  buffer[68] ='\0';
  return buffer;
}
