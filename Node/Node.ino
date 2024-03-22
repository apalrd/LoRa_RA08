uint32_t next_event;
uint16_t event_counter;
static const uint32_t event_delay = 1000*10;

/* Delay after bootup */
static const uint32_t initial_delay = 1000*5;

/* How often we request ack */
static const uint8_t event_ack = 30;

/* How many retries we should do */
static const uint8_t event_retries = 3;
uint32_t repeat_time;

/* Flag to echo messages */
static bool ra08_debug = true;

/* Buffer size */
#define RA08_BUF_SIZE 256

/* Frequency (1-8) - note that some setups use 0-7 instead */
#define RA08_FREQ 2

/* Timeout to join and time delay after join */
#define RA08_JOIN_TIMEOUT 45
#define RA08_JOIN_DELAY 1

/* Timeout to send and holdoff after sending */
#define RA08_SEND_TIMEOUT 6
#define RA08_SEND_HOLDOFF 4

/* Joined success */
static bool ra08_join = false;

/* Error return types */
enum {
  RA08_NO_ERR = 0,
  RA08_CJOIN_OK,
  RA08_RET_ERR,
  RA08_TIMEOUT,
};

void ra08_flush()
{
  while(Serial1.available()) Serial1.read();
}

/* Parse response from RA-08H */
int ra08_resp(double timeout = 2)
{
  /* Store time we need to timeout */
  uint32_t time_end = millis() + timeout*1000.0;
  /* Character buffer */
  static char chrbuf[RA08_BUF_SIZE] = {0};
  static int chridx = 0;
  static char debugout[RA08_BUF_SIZE];
  /* Continue to accumulate bytes until we have a newline, then process that string */
  while(millis() < time_end)
  {
    /* Check if we should add a character to the buffer */
    if(Serial1.available())
    {
      chrbuf[chridx]=Serial1.read();
      //Serial.write(chrbuf[chridx]);
      chridx++;

      /* If we reach the end of the buffer, something is wrong, scrap the whole thing */
      if(chridx >= RA08_BUF_SIZE)
      {
        chridx = 0;
        chrbuf[chridx] = 0;
        Serial.write("Buffer overflow\n");
      }
    }

    /* Check if the end of the buffer is now a newline character, meaning we should process this line */
    if((chridx > 0) && (chrbuf[chridx-1] == '\n'))
    {
      //Serial.write("DEBUG: Got a whole line: ");
      chrbuf[chridx] = 0;
      //Serial.write(chrbuf);

      /* Check if this contains an OK+ */
      if(strncmp(chrbuf,"OK",2) == 0)
      {
        sprintf(debugout,"Got OK response: %s",chrbuf);
        Serial.write(debugout);
        /* Reset the buffer */
        chridx = 0;
        chrbuf[chridx] = 0;

        return RA08_NO_ERR;
      }

      /* Check if it contains an ERR+ or +CME */
      if((strncmp(chrbuf,"ERR+",4) == 0) || (strncmp(chrbuf,"+CME",4) == 0))
      {
        sprintf(debugout,"Got ERR response: %s",chrbuf);
        Serial.write(debugout);
        /* Reset the buffer */
        chridx = 0;
        chrbuf[chridx] = 0;
        return RA08_RET_ERR;
      }

      /* Check if it contains an +CJOIN:OK */
      if(strncmp(chrbuf,"+CJOIN:OK",9) == 0)
      {
        sprintf(debugout,"Got CJOIN OK response: %s",chrbuf);
        Serial.write(debugout);
        /* Reset the buffer */
        chridx = 0;
        chrbuf[chridx] = 0;
        return RA08_CJOIN_OK;
      }

      /* Not sure what we got, but discard it if it's not a blank line */
      if(chrbuf[0] != '\r')
      {
        sprintf(debugout,"Not sure what we got, discarding it: %s\n",chrbuf);
        Serial.write(debugout);
      }
      chridx = 0;
      chrbuf[chridx] = 0;

    }
  }
  chrbuf[chridx] = 0;
  sprintf(debugout,"DEBUG: RA08 Timeout, buf has %d chars (%s)\n",chridx,chrbuf);
  Serial.write(debugout);
  return RA08_TIMEOUT;
}

void ra08_init()
{
  char out[RA08_BUF_SIZE];

  /* Setup serial port */
  Serial1.setRX(1);
  Serial1.setTX(0);
  Serial1.begin(9600);
  /* Ask for version number, reset if we get an error */
  bool offline = true;
  while(offline)
  {
    Serial1.write("AT+CGMR?\n");
    if(!ra08_resp(2))
    {
      offline = false;
    }
    else
    {
      Serial.write("Version query failed, resetting\n");
      Serial1.write("\n\nAT+IREBOOT=1\n");
      uint32_t end_delay = millis() + 10000;
      while(millis() <= end_delay)
      {
        //Echo back to user
        if (Serial1.available())
        {
          Serial.write(Serial1.read());
        }
      }
    }
  }
  /* Set debug mode to 2 */
  Serial1.write("AT+ILOGLVL=2\n");
  ra08_resp(2);

  /* Set other parameters to do network join */
  Serial1.write("AT+CJOINMODE=0\n");
  ra08_resp();
  Serial1.write("AT+CULDLMODE=2\n");
  ra08_resp();
  Serial1.write("AT+CCLASS=0\n");
  ra08_resp();
  sprintf(out,"AT+CFREQBANDMASK=%04x\n",(1<<(RA08_FREQ-1)));
  Serial1.write(out);
  ra08_resp();


  /* Do network join */
  sprintf(out,"AT+CJOIN=1,0,8,8\n");
  Serial1.write(out);
  Serial.write("Doing Network JOIN\n");
  /* Wait for it to ack the command */
  ra08_resp();
  /* Then wait again for it to join */
  if(ra08_resp(RA08_JOIN_TIMEOUT) != RA08_CJOIN_OK)
  {
    Serial.write("JOIN FAILED, will try again later\n");
  }
  else
  {
    Serial.write("JOIN SUCCESS\n");
    ra08_join = true;
    delay(RA08_JOIN_DELAY*1000);
    /* Flush */
    ra08_flush();
  }
}

/* Transmit a message
 * Size is in BYTES
 */
int ra08_xmit(const char port, const char * buf, const char size, const char retries = 2,const bool ack = false)
{
  char out[RA08_BUF_SIZE];
  sprintf(out,"GOT A REQUEST to transmit %d bytes on port %d with ack %d\n",size,port,ack);
  Serial.write(out);
  /* If we are offline, give up */
  if(!ra08_join)
  {
    Serial.write("Failed due to lack of join\n");
    return -1;
  }

  /* Set port */
  sprintf(out,"AT+CAPPPORT=%d\n",port);
  Serial1.write(out);
  ra08_resp(2);

  /* Request transmission */
  sprintf(out,"AT+DTRX=%d,%d,%d,",ack,retries,size*2);
  for(int i = 0;i < size; i++)
  {
    sprintf(out,"%s%02x",out,buf[i]);
  }
  strcat(out,"\n");
  Serial.write(out);
  Serial1.write(out);
  /* If we ack, we need 3 - OK+SENT (short), OK+SEND, OK+RECV */
  if(ack)
  {
    /* Wait for OK */
    if(ra08_resp())
    {
      Serial.write("Failed to accept transmission\n");
      return -1;
    }
    /* Wait for actual send */
    if(ra08_resp(RA08_SEND_TIMEOUT*retries))
    {
      Serial.write("Failed to send transmission\n");
      return -1;
    }
    /* Wait for send recv to come back */
    if(ra08_resp(RA08_SEND_TIMEOUT*retries))
    {
      Serial.write("Failed to send/recv transmission\n");
      return -1;
    }
  }
  /* If we don't ack, we only need OK+SENT at full length */
  else
  {
    /* Wait for actual send */
    if(ra08_resp(RA08_SEND_TIMEOUT*retries))
    {
      Serial.write("Failed to send transmission no-ack\n");
      return -1;
    }
  }

  Serial1.write("\n");
  //delay(1000*RA08_SEND_HOLDOFF*retries);
  Serial.write("Transmission Successful\n");

  return 0;
}

void blink_num(int num)
{
  static const int pin_led = 25;
  static const double blink_time = 0.69;
  static const double blink_off = 0.33;
  static const double blink_time_tens = 1.6;
  static const double blink_off_tens = 0.69;
  pinMode(pin_led,OUTPUT);
  //Tens
  int tens = num / 10;
  int ones = num - (tens * 10);
  for(int i = 0; i < tens; i++)
  {
    digitalWrite(pin_led,false);
    delay(blink_time_tens * 1000);
    digitalWrite(pin_led,true);
    delay(blink_off_tens * 1000);
  } 
  //ones
  for(int i = 0; i < ones; i++)
  {
    digitalWrite(pin_led,false);
    delay(blink_time * 1000);
    digitalWrite(pin_led,true);
    delay(blink_off * 1000);
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

  //Wait for the serial terminal to come up so we don't miss anything important
  for(int i = 0; i < 4; i++)
  {
    delay(1000);
    Serial.print("Waiting....\n");
  }

  /* Setup RA08 and do initial join */
  ra08_init();
  //Set next event in the futur
  next_event = millis()+initial_delay;
}

void loop() {

  uint32_t now = millis();
  //If time to do a transmit
  if((now >= next_event))
  {
    //Do it again in the future
    next_event += event_delay;
    
    //Buffer for some chars
    char atcmd[256];

    /* Require Ack on some things */
    int ack = (event_counter % event_ack) == 0;

    /* Debug message only if sent with ack */
    if(ack)
    {
      sprintf(atcmd,"DEBUG: Sending port %d data %d with ack\n",1,event_counter);
      Serial.write(atcmd);
    }

    /* Transmit */
    uint16_t data = htons(event_counter);
    ra08_xmit(1,(char *)&data,2,event_retries,ack);

    blink_num(event_counter);

    /* Increment counter */
    event_counter++;
  }
  
  /* And now do it again */
}
