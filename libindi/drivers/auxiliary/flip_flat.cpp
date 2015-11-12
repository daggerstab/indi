/*******************************************************************************
  Copyright(c) 2015 Jasem Mutlaq. All rights reserved.

  Simple GPS Simulator

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU Library General Public License
  along with this library; see the file COPYING.LIB.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
*******************************************************************************/

#include <memory>
#include <libnova.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "indicom.h"

#include "flip_flat.h"

// We declare an auto pointer to FlipFlat.
std::unique_ptr<FlipFlat> flipflat(new FlipFlat());

#define FLAT_CMD        6
#define FLAT_RES        8
#define FLAT_TIMEOUT    3
#define POLLMS          1000

void ISGetProperties(const char *dev)
{
    flipflat->ISGetProperties(dev);
}

void ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int num)
{
    flipflat->ISNewSwitch(dev, name, states, names, num);
}

void ISNewText(	const char *dev, const char *name, char *texts[], char *names[], int num)
{
    flipflat->ISNewText(dev, name, texts, names, num);
}

void ISNewNumber(const char *dev, const char *name, double values[], char *names[], int num)
{
    flipflat->ISNewNumber(dev, name, values, names, num);
}

void ISNewBLOB (const char *dev, const char *name, int sizes[], int blobsizes[], char *blobs[], char *formats[], char *names[], int n)
{
    INDI_UNUSED(dev);
    INDI_UNUSED(name);
    INDI_UNUSED(sizes);
    INDI_UNUSED(blobsizes);
    INDI_UNUSED(blobs);
    INDI_UNUSED(formats);
    INDI_UNUSED(names);
    INDI_UNUSED(n);
}
void ISSnoopDevice (XMLEle *root)
{
    INDI_UNUSED(root);
}

FlipFlat::FlipFlat()
{
    setVersion(1,0);
    PortFD=-1;
    isFlipFlat=false;
    prevCoverStatus = prevLightStatus = prevMotorStatus = prevBrightness = 0xFF;
}

FlipFlat::~FlipFlat()
{

}

bool FlipFlat::initProperties()
{
    INDI::DefaultDevice::initProperties();

    // Device port
    IUFillText(&PortT[0],"PORT","Port","/dev/ttyUSB0");
    IUFillTextVector(&PortTP,PortT,1,getDeviceName(),"DEVICE_PORT","Ports",OPTIONS_TAB,IP_RW,60,IPS_IDLE);

    // Open/Close cover
    IUFillSwitch(&CoverS[0], "DUST_COVER_OPEN", "Open", ISS_OFF);
    IUFillSwitch(&CoverS[1], "DUST_COVER_CLOSE", "Close", ISS_OFF);
    IUFillSwitchVector(&CoverSP, CoverS, 2, getDeviceName(), "DUST_COVER", "Dust Cover", MAIN_CONTROL_TAB, IP_RW, ISR_ATMOST1, 0, IPS_IDLE);

    // Turn on/off light
    IUFillSwitch(&LightS[0], "FLAT_LIGHT_ON", "On", ISS_OFF);
    IUFillSwitch(&LightS[1], "FLAT_LIGHT_OFF", "Off", ISS_OFF);
    IUFillSwitchVector(&LightSP, LightS, 2, getDeviceName(), "FLAT_LIGHT_CONTROL", "Flat Light", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);

    // Light Intensity
    IUFillNumber(&LightIntensityN[0], "FLAT_LIGHT_INTENSITY_VALUE", "Value", "%.f", 0, 255, 10, 0);
    IUFillNumberVector(&LightIntensityNP, LightIntensityN, 1, getDeviceName(), "FLAT_LIGHT_INTENSITY", "Brightness", MAIN_CONTROL_TAB, IP_RW, 0, IPS_IDLE);

    // Status
    IUFillText(&StatusT[0],"Cover","",NULL);
    IUFillText(&StatusT[1],"Light","",NULL);
    IUFillText(&StatusT[2],"Motor","",NULL);
    IUFillTextVector(&StatusTP,StatusT,3,getDeviceName(),"Status","",MAIN_CONTROL_TAB,IP_RO,60,IPS_IDLE);

    // Firmware version
    IUFillText(&FirmwareT[0],"Version","",NULL);
    IUFillTextVector(&FirmwareTP,FirmwareT,1,getDeviceName(),"Firmware","",MAIN_CONTROL_TAB,IP_RO,60,IPS_IDLE);

    setDriverInterface(AUX_INTERFACE);

    addDebugControl();

    return true;
}

void FlipFlat::ISGetProperties (const char *dev)
{
    INDI::DefaultDevice::ISGetProperties(dev);

    defineText(&PortTP);
    loadConfig(true, "DEVICE_PORT");
}

bool FlipFlat::updateProperties()
{
    INDI::DefaultDevice::updateProperties();

    if (isConnected())
    {
        if (isFlipFlat)
            defineSwitch(&CoverSP);
        defineSwitch(&LightSP);
        defineNumber(&LightIntensityNP);
        defineText(&StatusTP);
        defineText(&FirmwareTP);

        getStartupData();
    }
    else
    {
        if (isFlipFlat)
            deleteProperty(CoverSP.name);
        deleteProperty(LightSP.name);
        deleteProperty(LightIntensityNP.name);
        deleteProperty(StatusTP.name);
        deleteProperty(FirmwareTP.name);

    }

    return true;
}


const char * FlipFlat::getDefaultName()
{
    return (char *)"Flip Flat";
}

bool FlipFlat::Connect()
{
    int connectrc=0;
    char errorMsg[MAXRBUF];

    if (connectrc = tty_connect(PortT[0].text, 9600, 8, 0, 1, &PortFD) != TTY_OK)
    {
        tty_error_msg(connectrc, errorMsg, MAXRBUF);

        DEBUGF(INDI::Logger::DBG_SESSION, "Failed to connect to port %s. Error: %s", PortT[0].text, errorMsg);

        return false;
    }

    /* Drop RTS */
    int i = 0;
    i |= TIOCM_RTS;
    if (ioctl(PortFD, TIOCMBIC, &i) != 0)
    {
            DEBUG(INDI::Logger::DBG_ERROR, "IOCTL error.");
            return false;
    }

    i |= TIOCM_RTS;
    int rts=0;
    rts = ioctl(PortFD, TIOCMGET, &i);

    if (ping() == false)
    {
        DEBUG(INDI::Logger::DBG_ERROR, "Device ping failed.");
        return false;
    }

    DEBUGF(INDI::Logger::DBG_SESSION, "Connected successfuly to %s. Retrieving startup data...", getDeviceName());

    SetTimer(POLLMS);

    return true;
}

bool FlipFlat::Disconnect()
{
    tty_disconnect(PortFD);

    DEBUGF(INDI::Logger::DBG_SESSION,"%s is offline.", getDeviceName());

    return true;
}

bool FlipFlat::ISNewNumber (const char *dev, const char *name, double values[], char *names[], int n)
{
    if(strcmp(dev,getDeviceName())==0)
    {
        if (!strcmp(LightIntensityNP.name, name))
        {
            double prevValue = LightIntensityN[0].value;
            IUUpdateNumber(&LightIntensityNP, values, names, n);

            bool rc = setBrightness(LightIntensityN[0].value);
            if (rc)
                LightIntensityNP.s = IPS_OK;
            else
            {
                LightIntensityN[0].value = prevValue;
                LightIntensityNP.s = IPS_ALERT;
            }

            IDSetNumber(&LightIntensityNP, NULL);

            return true;
        }
    }

    return INDI::DefaultDevice::ISNewNumber(dev, name, values, names, n);

}

bool FlipFlat::ISNewText (const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if(strcmp(dev,getDeviceName())==0)
    {
        if (!strcmp(PortTP.name, name))
        {
            IUUpdateText(&PortTP, texts, names, n);
            PortTP.s = IPS_OK;
            IDSetText(&PortTP, NULL);
            return true;
        }
    }

    return INDI::DefaultDevice::ISNewText(dev, name, texts, names, n);
}

bool FlipFlat::ISNewSwitch (const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if(strcmp(dev,getDeviceName())==0)
    {
        // Cover
        if (!strcmp(CoverSP.name, name))
        {
            IUUpdateSwitch(&CoverSP, states, names, n);
            bool rc = controlCover(CoverS[0].s == ISS_ON ? OPEN_COVER : CLOSE_COVER);
            IUResetSwitch(&CoverSP);

            CoverSP.s = rc ? IPS_BUSY : IPS_ALERT;

            IDSetSwitch(&CoverSP, NULL);

            return true;
        }

        // Cover
        if (!strcmp(LightSP.name, name))
        {
            int prevIndex = IUFindOnSwitchIndex(&LightSP);
            IUUpdateSwitch(&LightSP, states, names, n);
            bool rc = controlLight(LightS[0].s == ISS_ON ? TURN_ON_LIGHT : TURN_OFF_LIGHT);

            LightSP.s = rc ? IPS_OK : IPS_ALERT;

            if (rc == false)
            {
                IUResetSwitch(&LightSP);
                LightS[prevIndex].s = ISS_ON;
            }

            IDSetSwitch(&LightSP, NULL);

            return true;
        }
    }

    return INDI::DefaultDevice::ISNewSwitch(dev, name, states, names, n);
}

bool FlipFlat::ping()
{
    int nbytes_written=0, nbytes_read=0, rc=-1;
    char errstr[MAXRBUF];
    char command[FLAT_CMD];
    char response[FLAT_RES];
    int i=0;

    tty_set_debug(1);

    tcflush(PortFD, TCIOFLUSH);

    strncpy(command, ">P000", FLAT_CMD);

    DEBUGF(INDI::Logger::DBG_DEBUG, "CMD (%s)", command);

    command[FLAT_CMD-1] = 0xA;

    for (i=0; i < 3; i++)
    {
        if ( (rc = tty_write(PortFD, command, FLAT_CMD, &nbytes_written)) != TTY_OK)
            continue;

        if ( (rc = tty_read_section(PortFD, response, 0xA, 1, &nbytes_read)) != TTY_OK)
            continue;
        else
            break;
    }

    if (i==3)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        DEBUGF(INDI::Logger::DBG_ERROR, "%s: %s.", command, errstr);
        return false;
    }

    response[nbytes_read-1] = '\0';

    DEBUGF(INDI::Logger::DBG_DEBUG, "RES (%s)", response);

    char productString[3];
    snprintf(productString, 3, "%s", response+2);

    rc = sscanf(productString, "%d", &productID);

    if (rc <= 0)
    {
        DEBUGF(INDI::Logger::DBG_ERROR, "Unable to parse input (%s)", response);
        return false;
    }

    if (productID == 99)
        isFlipFlat = true;
    else
        isFlipFlat = false;

    return true;
}

bool FlipFlat::getStartupData()
{
    bool rc1 = getFirmwareVersion();
    bool rc2 = getStatus();
    bool rc3 = getBrightness();

    return (rc1 && rc2 && rc3);
}

bool FlipFlat::controlCover(int cmd)
{
    int nbytes_written=0, nbytes_read=0, rc=-1;
    char errstr[MAXRBUF];
    char command[FLAT_CMD];
    char response[FLAT_RES];

    tcflush(PortFD, TCIOFLUSH);

    if (cmd == OPEN_COVER)
        strncpy(command, ">O000", FLAT_CMD);
    else
        strncpy(command, ">C000", FLAT_CMD);

    DEBUGF(INDI::Logger::DBG_DEBUG, "CMD (%s)", command);

    command[FLAT_CMD-1] = 0xA;

    if ( (rc = tty_write(PortFD, command, FLAT_CMD, &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        DEBUGF(INDI::Logger::DBG_ERROR, "%s error: %s.", command, errstr);
        return false;
    }

    if ( (rc = tty_read_section(PortFD, response, 0xA, FLAT_TIMEOUT, &nbytes_read)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        DEBUGF(INDI::Logger::DBG_ERROR, "%s: %s.", command, errstr);
        return false;
    }

    response[nbytes_read-1] = '\0';

    DEBUGF(INDI::Logger::DBG_DEBUG, "RES (%s)", response);

    char expectedResponse[FLAT_RES];
    if (cmd == OPEN_COVER)
        snprintf(expectedResponse, FLAT_RES, "*O%02d000", productID);
    else
        snprintf(expectedResponse, FLAT_RES, "*C%02d000", productID);

    if (!strcmp(response, expectedResponse))
        return true;
    else
        return false;

}

bool FlipFlat::controlLight(int cmd)
{
    int nbytes_written=0, nbytes_read=0, rc=-1;
    char errstr[MAXRBUF];
    char command[FLAT_CMD];
    char response[FLAT_RES];

    tcflush(PortFD, TCIOFLUSH);

    if (cmd == TURN_ON_LIGHT)
        strncpy(command, ">L000", FLAT_CMD);
    else
        strncpy(command, ">D000", FLAT_CMD);

    DEBUGF(INDI::Logger::DBG_DEBUG, "CMD (%s)", command);

    command[FLAT_CMD-1] = 0xA;

    if ( (rc = tty_write(PortFD, command, FLAT_CMD, &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        DEBUGF(INDI::Logger::DBG_ERROR, "%s error: %s.", command, errstr);
        return false;
    }

    if ( (rc = tty_read_section(PortFD, response, 0xA, FLAT_TIMEOUT, &nbytes_read)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        DEBUGF(INDI::Logger::DBG_ERROR, "%s: %s.", command, errstr);
        return false;
    }

    response[nbytes_read-1] = '\0';

    DEBUGF(INDI::Logger::DBG_DEBUG, "RES (%s)", response);

    char expectedResponse[FLAT_RES];
    if (cmd == TURN_ON_LIGHT)
        snprintf(expectedResponse, FLAT_RES, "*L%02d000", productID);
    else
        snprintf(expectedResponse, FLAT_RES, "*D%02d000", productID);

    if (!strcmp(response, expectedResponse))
        return true;
    else
        return false;

}

bool FlipFlat::getStatus()
{
    int nbytes_written=0, nbytes_read=0, rc=-1;
    char errstr[MAXRBUF];
    char command[FLAT_CMD];
    char response[FLAT_RES];

    tcflush(PortFD, TCIOFLUSH);

    strncpy(command, ">S000", FLAT_CMD);

    DEBUGF(INDI::Logger::DBG_DEBUG, "CMD (%s)", command);

    command[FLAT_CMD-1] = 0xA;

    if ( (rc = tty_write(PortFD, command, FLAT_CMD, &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        DEBUGF(INDI::Logger::DBG_ERROR, "%s error: %s.", command, errstr);
        return false;
    }

    if ( (rc = tty_read_section(PortFD, response, 0xA, FLAT_TIMEOUT, &nbytes_read)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        DEBUGF(INDI::Logger::DBG_ERROR, "%s: %s.", command, errstr);
        return false;
    }

    response[nbytes_read-1] = '\0';

    DEBUGF(INDI::Logger::DBG_DEBUG, "RES (%s)", response);

    char motorStatus = *(response+4) - '0';
    char lightStatus = *(response+5) - '0';
    char coverStatus = *(response+6) - '0';

    bool statusUpdated = false;

    if (coverStatus != prevCoverStatus)
    {
        prevCoverStatus = coverStatus;

        statusUpdated = true;

        switch (coverStatus)
        {
        case 0:
            IUSaveText(&StatusT[0], "Not Open/Closed");
            break;

        case 1:
            IUSaveText(&StatusT[0], "Closed");
            if (CoverSP.s == IPS_BUSY || CoverSP.s == IPS_IDLE)
            {
                IUResetSwitch(&CoverSP);
                CoverS[1].s = ISS_ON;
                CoverSP.s = IPS_OK;
                DEBUG(INDI::Logger::DBG_SESSION, "Cover closed.");
                IDSetSwitch(&CoverSP, NULL);
            }
            break;

        case 2:
            IUSaveText(&StatusT[0], "Open");
            if (CoverSP.s == IPS_BUSY || CoverSP.s == IPS_IDLE)
            {
                IUResetSwitch(&CoverSP);
                CoverS[0].s = ISS_ON;
                CoverSP.s = IPS_OK;
                DEBUG(INDI::Logger::DBG_SESSION, "Cover open.");
                IDSetSwitch(&CoverSP, NULL);
            }
            break;

        case 3:
            IUSaveText(&StatusT[0], "Timed out");
            break;

        }
    }

    if (lightStatus != prevLightStatus)
    {
        prevLightStatus = lightStatus;

        statusUpdated = true;

        switch (lightStatus)
        {
        case 0:
            IUSaveText(&StatusT[1], "Off");
            if (LightS[0].s == ISS_ON)
            {
                LightS[0].s = ISS_OFF;
                LightS[1].s = ISS_ON;
                IDSetSwitch(&LightSP, NULL);
            }
            break;

        case 1:
            IUSaveText(&StatusT[1], "On");
            if (LightS[1].s == ISS_ON)
            {
                LightS[0].s = ISS_ON;
                LightS[1].s = ISS_OFF;
                IDSetSwitch(&LightSP, NULL);
            }
            break;
        }
    }

    if (motorStatus != prevMotorStatus)
    {
        prevMotorStatus = motorStatus;

        statusUpdated = true;

        switch (motorStatus)
        {
        case 0:
            IUSaveText(&StatusT[2], "Stopped");
            break;

        case 1:
            IUSaveText(&StatusT[2], "Running");
            break;

        }
    }

    if (statusUpdated)
        IDSetText(&StatusTP, NULL);

    return true;
}

bool FlipFlat::getFirmwareVersion()
{
    int nbytes_written=0, nbytes_read=0, rc=-1;
    char errstr[MAXRBUF];
    char command[FLAT_CMD];
    char response[FLAT_RES];

    tcflush(PortFD, TCIOFLUSH);

    strncpy(command, ">V000", FLAT_CMD);

    DEBUGF(INDI::Logger::DBG_DEBUG, "CMD (%s)", command);

    command[FLAT_CMD-1] = 0xA;

    if ( (rc = tty_write(PortFD, command, FLAT_CMD, &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        DEBUGF(INDI::Logger::DBG_ERROR, "%s error: %s.", command, errstr);
        return false;
    }

    if ( (rc = tty_read_section(PortFD, response, 0xA, FLAT_TIMEOUT, &nbytes_read)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        DEBUGF(INDI::Logger::DBG_ERROR, "%s: %s.", command, errstr);
        return false;
    }

    response[nbytes_read-1] = '\0';

    DEBUGF(INDI::Logger::DBG_DEBUG, "RES (%s)", response);

    char versionString[4];
    snprintf(versionString, 4, "%s", response+4 );
    IUSaveText(&FirmwareT[0], versionString);
    IDSetText(&FirmwareTP, NULL);

    return true;
}

void FlipFlat::TimerHit()
{
    if (isConnected() == false)
        return;

    getStatus();

    SetTimer(POLLMS);
}

bool FlipFlat::getBrightness()
{
    int nbytes_written=0, nbytes_read=0, rc=-1;
    char errstr[MAXRBUF];
    char command[FLAT_CMD];
    char response[FLAT_RES];

    tcflush(PortFD, TCIOFLUSH);

    strncpy(command, ">J000", FLAT_CMD);

    DEBUGF(INDI::Logger::DBG_DEBUG, "CMD (%s)", command);

    command[FLAT_CMD-1] = 0xA;

    if ( (rc = tty_write(PortFD, command, FLAT_CMD, &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        DEBUGF(INDI::Logger::DBG_ERROR, "%s error: %s.", command, errstr);
        return false;
    }

    if ( (rc = tty_read_section(PortFD, response, 0xA, FLAT_TIMEOUT, &nbytes_read)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        DEBUGF(INDI::Logger::DBG_ERROR, "%s: %s.", command, errstr);
        return false;
    }

    response[nbytes_read-1] = '\0';

    DEBUGF(INDI::Logger::DBG_DEBUG, "RES (%s)", response);

    char brightnessString[4];
    snprintf(brightnessString, 4, "%s", response+4 );

    int brightnessValue=0;
    rc = sscanf(brightnessString, "%d", &brightnessValue);

    if (rc <= 0)
    {
        DEBUGF(INDI::Logger::DBG_ERROR, "Unable to parse brightness value (%s)", response);
        return false;
    }

    if (brightnessValue != prevBrightness)
    {
        prevBrightness = brightnessValue;
        LightIntensityN[0].value = brightnessValue;
        IDSetNumber(&LightIntensityNP, NULL);
    }

    return true;
}

bool FlipFlat::setBrightness(int value)
{
    int nbytes_written=0, nbytes_read=0, rc=-1;
    char errstr[MAXRBUF];
    char command[FLAT_CMD];
    char response[FLAT_RES];

    tcflush(PortFD, TCIOFLUSH);

    snprintf(command, FLAT_CMD, ">B%03d", value);

    DEBUGF(INDI::Logger::DBG_DEBUG, "CMD (%s)", command);

    command[FLAT_CMD-1] = 0xA;

    if ( (rc = tty_write(PortFD, command, FLAT_CMD, &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        DEBUGF(INDI::Logger::DBG_ERROR, "%s error: %s.", command, errstr);
        return false;
    }

    if ( (rc = tty_read_section(PortFD, response, 0xA, FLAT_TIMEOUT, &nbytes_read)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        DEBUGF(INDI::Logger::DBG_ERROR, "%s: %s.", command, errstr);
        return false;
    }

    response[nbytes_read-1] = '\0';

    DEBUGF(INDI::Logger::DBG_DEBUG, "RES (%s)", response);

    char brightnessString[4];
    snprintf(brightnessString, 4, "%s", response+4 );

    int brightnessValue=0;
    rc = sscanf(brightnessString, "%d", &brightnessValue);

    if (rc <= 0)
    {
        DEBUGF(INDI::Logger::DBG_ERROR, "Unable to parse brightness value (%s)", response);
        return false;
    }

    if (brightnessValue != prevBrightness)
    {
        prevBrightness = brightnessValue;
        LightIntensityN[0].value = brightnessValue;
        IDSetNumber(&LightIntensityNP, NULL);
    }

    return true;

}