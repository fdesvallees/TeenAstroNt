#include "Global.h"


void EqMount::axesToSteps(Axes *aP, Steps *sP)
{
  sP->steps1 = (long)((aP->axis1) * geoA1.stepsPerDegree);
  sP->steps2 = (long)((aP->axis2) * geoA2.stepsPerDegree);
}

void EqMount::stepsToAxes(Steps *sP, Axes *aP)
{
  aP->axis1 = ((double)(sP->steps1) / geoA1.stepsPerDegree);
  aP->axis2 = ((double)(sP->steps2) / geoA2.stepsPerDegree);
}

/*
 * Our definition for the flipped condition: if axis2 position is negative (for N hemisphere)
 */
bool EqMount::isFlipped()
{
  long pos = motorA2.getCurrentPos();
 	return ((pos <= 0) == (*localSite.latitude() >= 0));	
}

PierSide EqMount::GetPierSide()
{
 	return (isFlipped()) ? PIER_WEST : PIER_EAST;
}

PierSide EqMount::otherSide(PierSide ps)
{
  return (ps == PIER_EAST ? PIER_WEST : PIER_EAST);
}

byte EqMount::goToEqu(EqCoords *eP)
{
	Axes axes;
	Steps steps;
	PierSide ps;
  HorCoords hor;

  // check destination altitude
  EquToHor(eP->ha, eP->dec, false, &hor.az, &hor.alt, localSite.cosLat(), localSite.sinLat());
  if (hor.alt < limits.minAlt)
    return ERRGOTO_BELOWHORIZON;
  if (hor.alt > limits.maxAlt)
    return ERRGOTO_ABOVEOVERHEAD;

	ps = GetPierSide();
  if (!eqToAxes(eP, &axes, ps))                 // try current side
  {
    if (!eqToAxes(eP, &axes, otherSide(ps)))    // try other side
      return ERRGOTO_LIMITS;                    //fail, outside limit
  }
  axesToSteps(&axes, &steps);

  return goTo(&steps);
}

byte EqMount::goToHor(HorCoords *hP)
{
	EqCoords eq;
	
	HorTopoToEqu(hP->az, hP->alt, &eq.ha, &eq.dec, localSite.cosLat(), localSite.sinLat());
	return goToEqu(&eq);
}


/*
 * Find on which pier side the target will be 
 *
 */
bool EqMount::getTargetPierSide(EqCoords *eP, PierSide *psOutP)
{
  Axes axes;
  PierSide ps = GetPierSide();

  if (!eqToAxes(eP, &axes, ps))                 // try current side
  {
    ps = otherSide(ps);
    if (!eqToAxes(eP, &axes, ps))                 // try other side
      return false;                             //fail, outside limit
  }
  *psOutP = ps;
  return true;
}


/*
 * eqToAxes: computes axes from equatorial coordinates
 * return false if we cannot reach the given position on the given side
 */
bool EqMount::eqToAxes(EqCoords *eP, Axes *aP, PierSide ps)
{  
  int hemisphere = (*localSite.latitude() >= 0) ? 1 : -1;                     // 1 for Northern Hemisphere
  if (type == MOUNT_TYPE_GEM)
  {
    int flipSign = ((ps==PIER_EAST)==(*localSite.latitude() >= 0)) ? 1 : -1;    // -1 if mount is flipped

    aP->axis1 = hemisphere * eP->ha  - flipSign * 90;
    aP->axis2 = flipSign * (90 - hemisphere * eP->dec);  

    if (!withinLimits(aP->axis1, aP->axis2))
      return false;

    if (!checkMeridian(aP, CHECKMODE_GOTO, ps))
      return false;

    if (!checkPole(aP->axis1, CHECKMODE_GOTO))
      return false;

    return true;    
  }
  else // eq fork
  {
    aP->axis1 = eP->ha;
    aP->axis2 = 90 - eP->dec;  

    if (!withinLimits(aP->axis1, aP->axis2))
      return false;

    if (!checkPole(aP->axis1, CHECKMODE_GOTO))
      return false;

    return true;    
  }
}

void EqMount::axesToEqu(Axes *aP, EqCoords *eP)
{
  int hemisphere = (*localSite.latitude() >= 0) ? 1 : -1;
  int flipSign = (aP->axis2 < 0) ? -1 : 1;
  if (type == MOUNT_TYPE_GEM)
  {
    eP->ha  = hemisphere * (aP->axis1 + flipSign * 90);
    eP->dec = hemisphere * (90 - (flipSign * aP->axis2));    
  }
  else // eq fork
  {
    eP->ha  = aP->axis1;
    eP->dec = 90 - aP->axis2;    
  }
}

bool EqMount::getEqu(double *haP, double *decP, UNUSED(const double *cosLatP), UNUSED(const double *sinLatP), bool returnHA)
{
	Steps steps;
	Axes axes;
	EqCoords eqCoords;

	steps.steps1 = motorA1.getCurrentPos();
	steps.steps2 = motorA2.getCurrentPos();

	stepsToAxes(&steps, &axes);
	axesToEqu(&axes, &eqCoords);
	*haP = eqCoords.ha;
	*decP = eqCoords.dec;

  if (!returnHA)	// return RA instead of HA
  {
    *haP = degRange(rtk.LST() * 15.0 - *haP);
  }
  return true;
}

bool EqMount::getHorApp(HorCoords *hP)
{
	double ha, dec;

	getEqu(&ha, &dec, localSite.cosLat(), localSite.sinLat(), true);
	EquToHor(ha, dec, false, &hP->az, &hP->alt, localSite.cosLat(), localSite.sinLat());

  return true;
}

byte EqMount::goTo(Steps *sP)
{
  if (getEvent(EV_ABORT))
    return ERRGOTO____;
  unsigned msg[CTL_MAX_MESSAGE_SIZE];
  msg[0] = CTL_MSG_GOTO; 
  msg[1] = sP->steps1;
  msg[2] = sP->steps2;
  xQueueSend(controlQueue, &msg, 0);

  waitSlewing();

  DecayModeGoto();
  return ERRGOTO_NONE;
}

bool EqMount::syncEqu(double HA, double Dec, PierSide Side, UNUSED(const double *cosLat), UNUSED(const double *sinLat))
{
	EqCoords eqCoords;
	Axes axes;
	Steps steps;

	eqCoords.ha = HA;
	eqCoords.dec = Dec;
	eqToAxes(&eqCoords, &axes, Side);
	axesToSteps(&axes, &steps);
	motorA1.setCurrentPos(steps.steps1); 
	motorA2.setCurrentPos(steps.steps2); 
  return true;
}

bool EqMount::syncAzAlt(double Azm, double Alt, PierSide Side)
{
	EqCoords eq;
	
	HorTopoToEqu(Azm, Alt, &eq.ha, &eq.dec, localSite.cosLat(), localSite.sinLat());
	return syncEqu(eq.ha, eq.dec, Side, localSite.cosLat(), localSite.sinLat());
}

byte EqMount::Flip()
{
#if 0  
  long axis1Flip, axis2Flip;
  PierSide selectedSide = PIER_NOTVALID;
  PierSide preferedPierSide = (GetPierSide() == PIER_EAST) ? PIER_WEST : PIER_EAST;
  double Axis1 = motorA1.getCurrentPos() / (double)geoA1.stepsPerDegree;
  double Axis2 = motorA1.getCurrentPos() / (double)geoA2.stepsPerDegree;
  if (!predictTarget(Axis1, Axis2, preferedPierSide, axis1Flip, axis2Flip, selectedSide))
  {
    return ERRGOTO_SAMESIDE;
  }
  return goTo(axis1Flip, axis2Flip);
#endif  
  return ERRGOTO_NONE;
}

bool EqMount::checkPole(long axis1, CheckMode mode)
{
  double underPoleLimit = (mode == CHECKMODE_GOTO) ? limits.underPoleLimitGOTO : limits.underPoleLimitGOTO + 5.0 / 60;
  return (axis1 > (-underPoleLimit * 15.0 * geoA1.stepsPerDegree)) && (axis1 < (underPoleLimit * 15.0 * geoA1.stepsPerDegree));
}



bool EqMount::checkMeridian(Axes *aP, CheckMode mode, PierSide ps)
{
	EqCoords eqCoords;
	axesToEqu(aP, &eqCoords);
  // limits are stored in minutes of RA. Divide by 4 to get degrees
  double DegreesPastMeridianW = ((mode == CHECKMODE_GOTO) ? limits.minutesPastMeridianGOTOW : limits.minutesPastMeridianGOTOW + 5)/4;
  double DegreesPastMeridianE = ((mode == CHECKMODE_GOTO) ? limits.minutesPastMeridianGOTOE : limits.minutesPastMeridianGOTOE + 5)/4;
  switch (ps)
  {
    case PIER_WEST:
      if (eqCoords.ha >  DegreesPastMeridianW) 
        return false;
      break;
    case PIER_EAST:
      if (eqCoords.ha < -DegreesPastMeridianE)
        return false;
      break;
    default:
        return false;
      break;
  }
  return true;
}

bool EqMount::withinLimits(long axis1, long axis2)
{
  if (! ((geoA1.withinLimits(axis1) && geoA2.withinLimits(axis2))))
    return false;
  return true;
}

void EqMount::startTracking(void)
{
  unsigned msg[CTL_MAX_MESSAGE_SIZE];
  msg[0] = CTL_MSG_START_TRACKING; 
  xQueueSend( controlQueue, &msg, 0);
}

void EqMount::stopTracking(void)
{
  unsigned msg[CTL_MAX_MESSAGE_SIZE];
  msg[0] = CTL_MSG_STOP_TRACKING; 
  xQueueSend( controlQueue, &msg, 0);
}

/*
 * setTrackingSpeed
 * speed is expressed as a multiple of sidereal speed 
 */
void EqMount::setTrackingSpeed(double speed)
{
  trackingSpeed = speed;					// remember to reset it after guiding etc.
  trackingSpeeds.speed1 = speed;	// multiple of sidereal
  trackingSpeeds.speed2 = 0;
}


void EqMount::MoveAxis1(const byte dir)
{
  MoveAxis1AtRate(guideRates[activeGuideRate], dir);
}

void EqMount::MoveAxis1AtRate(double speed, const byte dir)
{
  unsigned msg[CTL_MAX_MESSAGE_SIZE];

  msg[0] = CTL_MSG_MOVE_AXIS1; 
  msg[1] = (dir == 'w' ? geoA1.westDef : geoA1.eastDef);
  memcpy(&msg[2], &speed, sizeof(double));
  xQueueSend(controlQueue, &msg, 0);
}

void EqMount::StopAxis1()
{
  unsigned msg[CTL_MAX_MESSAGE_SIZE];
 
  msg[0] = CTL_MSG_STOP_AXIS1; 
  xQueueSend(controlQueue, &msg, 0);
}

void EqMount::MoveAxis2(const byte dir)
{
  MoveAxis2AtRate(guideRates[activeGuideRate], dir);
}

void EqMount::MoveAxis2AtRate(double speed, const byte dir)
{
  unsigned msg[CTL_MAX_MESSAGE_SIZE];
 	long target;

 	target = poleDir(dir);

  msg[0] = CTL_MSG_MOVE_AXIS2; 
  msg[1] = target;
  memcpy(&msg[2], &speed, sizeof(double));
  xQueueSend(controlQueue, &msg, 0);
}

void EqMount::StopAxis2()
{
  unsigned msg[CTL_MAX_MESSAGE_SIZE];
 
  msg[0] = CTL_MSG_STOP_AXIS2; 
  xQueueSend(controlQueue, &msg, 0);
}

/*
 * getTrackingSpeeds
 * called periodically by Control task
 * computes axes speeds (in steps) for both axes
 */
void EqMount::getTrackingSpeeds(Speeds *sP)
{
  sP->speed1 = trackingSpeeds.speed1 * geoA1.stepsPerSecond/SIDEREAL_SECOND;
  sP->speed2 = trackingSpeeds.speed2 * geoA2.stepsPerSecond/SIDEREAL_SECOND;
}

void EqMount::initHome(Steps *sP)
{
  if (type == MOUNT_TYPE_GEM)
  {
    sP->steps1 = 0;
    sP->steps2 = 0;
  }
  else // eq fork
  {
    sP->steps1 = geoA1.halfRot;
    sP->steps2 = 0;
  }
}

/*
 * startGuiding
 * add guiding speed to tracking speed
 */
void EqMount::startGuiding(char dir, int milliseconds)
{
  if (!isTracking())
    return;
  switch(dir)
  {
    case 'w':
      if (!axis1Guiding)
      {
        axis1Guiding = true;
        trackingSpeeds.speed1 = trackingSpeed + guidingSpeed; // multiple of sidereal
        xTimerChangePeriod(axis1Timer, milliseconds, 0);
        xTimerStart(axis1Timer, 0);
        setEvents(EV_GUIDING_W);
      }
      break;
    case 'e':
      if (!axis1Guiding)
      {
        axis1Guiding = true;
        trackingSpeeds.speed1 = trackingSpeed - guidingSpeed; 
        xTimerChangePeriod(axis1Timer, milliseconds, 0);
        xTimerStart(axis1Timer, 0);
        setEvents(EV_GUIDING_E);
      }
      break;
    case 'n':
      if (!axis2Guiding)
      {
        axis2Guiding = true;
        trackingSpeeds.speed2 = guidingSpeed; 
        xTimerChangePeriod(axis2Timer, milliseconds, 0);
        xTimerStart(axis2Timer, 0);
        setEvents(EV_GUIDING_N);
      }
      break;
    case 's':
      if (!axis2Guiding)
      {
        axis2Guiding = true;
        trackingSpeeds.speed2 = -guidingSpeed; 
        xTimerChangePeriod(axis2Timer, milliseconds, 0);
        xTimerStart(axis2Timer, 0);
        setEvents(EV_GUIDING_S);
      }
      break;
  }
}

// Callbacks for guiding timers - reset tracking speeds to default
void stopGuidingAxis1(UNUSED(TimerHandle_t xTimer))
{
  resetEvents(EV_GUIDING_W | EV_GUIDING_E);
  mount.mP->axis1Guiding = false;
  mount.mP->trackingSpeeds.speed1 = mount.mP->trackingSpeed; 
}

void stopGuidingAxis2(UNUSED(TimerHandle_t xTimer))
{
  resetEvents(EV_GUIDING_N | EV_GUIDING_S);
  mount.mP->axis2Guiding = false;
  mount.mP->trackingSpeeds.speed2 = 0; 
}

/*
 * Pole / Antipole direction
 * return the axis2 position in steps of the pole / antipole directions
 * 
 * pole: always 0
 * antipole:
 *   in N hemisphere: +∞ if PierSide=E (not flipped), -∞ if PierSide=W (flipped)
 *   in S hemisphere: reverse
 */
long EqMount::poleDir(char pole)
{
  
  if ((*localSite.latitude() >= 0) == (pole == 'n'))	// pole?
  {
 		return 0;
  }	

	long pos = motorA2.getCurrentPos();
 	return ((pos >= 0) == (*localSite.latitude() >= 0)) ? geoA2.stepsPerRot : -geoA2.stepsPerRot;
}


/*
 * decDirection
 *  1 if declination is increasing
 * -1 if decreasing
 *  0 if not changing
 */
int EqMount::decDirection(void)
{
  long pos = motorA2.getCurrentPos(); 
  double sp = motorA2.getSpeed();
  if (sp == 0.0)
    return 0;

  if (pos < 0)
    return (sp < 0.0 ? -1:1);
  else 
    return (sp < 0.0 ? 1:-1);
}
