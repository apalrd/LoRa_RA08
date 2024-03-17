/* Buffer size */
#define RA08_BUF_SIZE 256

/* Error return types */
enum {
  RA08_NO_ERR = 0,
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

/* Function to init the RA-08
 * If it doesn't respond, reset it
 */
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
}

/* Function to read the EUI64s and AppKey */
void ra08_readkey()
{
  char buf[RA08_BUF_SIZE];
  /* New DEV EUI */
  sprintf(buf,"AT+CDEVEUI?\n");
  Serial.write(buf);
  Serial1.write(buf);


  /* New APP EUI */
  sprintf(buf,"AT+CAPPEUI?\n");
  Serial.write(buf);
  Serial1.write(buf);

  /* New APP KEY */
  sprintf(buf,"AT+CAPPKEY?\n");
  Serial.write(buf);
  Serial1.write(buf);
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

  for(int i = 0; i < 10; i++)
  {
    delay(1000);
    Serial.print("Waiting....\n");
  }

  /* Setup RA08 and do initial join */
  ra08_init();

  /* Read back the random numbers */
  ra08_readkey();
}

void loop() {
  /* Pass AT commands back and forth */
  if(Serial.available()) Serial1.write(Serial.read());
  if(Serial1.available()) Serial.write(Serial1.read());
}
