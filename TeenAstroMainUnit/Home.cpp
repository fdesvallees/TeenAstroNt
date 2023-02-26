// -----------------------------------------------------------------------------------
// functions related to Homing the mount
#include <Global.h>

bool setHome()
{
  if ((parkStatus == PRK_UNPARKED) && !isSlewing())
  {
    lastsiderealTracking = isTracking();
    mount.mP->stopTracking();
//    siderealTracking = false;

    // don't worry about moving around: during parking pec is turned off and backlash is cleared (0) so that staA1.target/targetAxis2=staA1.pos/staA2.pos
    // this should handle getting us back to the home position for micro-step modes up to 256X
    // if sync anywhere is enabled use the corrected location
#if 0
    long h = (staA1.target / 1024L) * 1024L;
    long d = (staA2.target / 1024L) * 1024L;
    h /= pow(2, motorA1.micro);
    d /= pow(2, motorA2.micro);
    // store our position
    XEEPROM.writeLong(EE_homePosAxis1, h);
    XEEPROM.writeLong(EE_homePosAxis2, d);
    XEEPROM.write(EE_homeSaved, 1);
#endif
    initHome();
    if (lastsiderealTracking)
      mount.mP->startTracking();
    else
      mount.mP->stopTracking();

//    siderealTracking = lastsiderealTracking;
    return true;
  }

  return false;
}

// unset home position flag
void unsetHome()
{
  XEEPROM.write(EE_homeSaved, 0);
  initHome();
}

bool atHome(void)
{
  return getEvent(EV_AT_HOME);
}

// moves telescope to the home position, then stops tracking
bool goHome()
{
  unsigned msg[CTL_MAX_MESSAGE_SIZE];
  msg[0] = CTL_MSG_GOTO_HOME; 
  xQueueSend( controlQueue, &msg, 0);

  return true;
}

// resets telescope home position; user manually moves home position,
bool syncAtHome()
{
  if (isSlewing()) return false;  // fail, forcing home not allowed during a move
  // default values for state variables
  newTargetRA = 0;
  newTargetDec = 0;
  newTargetAlt = 0;
  newTargetAzm = 0;
  lastError = ERRT_NONE;

  parkStatus = PRK_UNPARKED;
  XEEPROM.write(EE_parkStatus, parkStatus);

  // update starting coordinates to reflect NCP or SCP polar home position
  motorA1.setCurrentPos(geoA1.homeDef);
  motorA2.setCurrentPos(geoA2.homeDef);
  motorA1.setTargetPos(geoA1.homeDef);
  motorA2.setTargetPos(geoA2.homeDef);

  // initialize/disable the stepper drivers
  DecayModeTracking();
  mount.mP->stopTracking();
//  siderealTracking = false;
  setEvents(EV_AT_HOME);
  return true;
}

// init the telescope home position;  if defined use the user defined home position
void initHome()
{
  homeSaved = XEEPROM.read(EE_homeSaved);
  if (homeSaved)
  {
    geoA1.homeDef = XEEPROM.readLong(EE_homePosAxis1)*pow(2, motorA1.micro);
    geoA2.homeDef = XEEPROM.readLong(EE_homePosAxis2)*pow(2, motorA2.micro);
  }
  else
  {
    Steps s;
    mount.mP->initHome(&s);
    geoA1.homeDef = s.steps1;
    geoA2.homeDef = s.steps2;
  }
  int hemisphere = (*localSite.latitude()>=0? 1 : -1);
  geoA1.westDef = hemisphere * geoA1.stepsPerRot;
  geoA1.eastDef = -hemisphere * geoA1.stepsPerRot;
}