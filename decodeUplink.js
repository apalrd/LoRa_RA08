// Decode uplink function.
  //
  // Input is an object with the following fields:
  // - bytes = Byte array containing the uplink payload, e.g. [255, 230, 255, 0]
  // - fPort = Uplink fPort.
  // - variables = Object containing the configured device variables.
  //
  // Output must be an object with the following fields:
  // - data = Object representing the decoded payload.
function decodeUplink(input) {
    var decode = {};
    //Sensor 1 - Counter
    if(input.fPort == 1)
    {
        //Counter is 16-bit
        decode.count = input.bytes[0]*256 + input.bytes[1];
        //Example of converting a value to floating point
        //The value 'on the wire' is still an integer, but multiplied by 100
        //This way, we can transmit up to the hundredths place
        decode.float = parseFloat((input.bytes[0]*256 + input.bytes[1]) / 100);
    }
    return { 
        data: decode
    };
}