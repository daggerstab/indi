/*******************************************************************************
  Copyright(c) 2015 Jasem Mutlaq. All rights reserved.

  INDI Weather Meta Driver. It watches up to 4 weather drivers and report worst case
  of each in a single property.

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

#ifndef WEATHERMETA_H
#define WEATHERMETA_H

#include "defaultdevice.h"

class WeatherMeta : public INDI::DefaultDevice
{
    public:
    WeatherMeta();
    virtual ~WeatherMeta();

    //  Generic indi device entries
    bool Connect();
    bool Disconnect();
    const char *getDefaultName();

    virtual bool ISSnoopDevice(XMLEle *root);

    virtual bool initProperties();
    virtual bool updateProperties();
    virtual void ISGetProperties (const char *dev);
    virtual bool ISNewText (const char *dev, const char *name, char *texts[], char *names[], int n);
    virtual bool ISNewNumber (const char *dev, const char *name, double values[], char *names[], int n);

    protected:

    virtual bool saveConfigItems(FILE *fp);

private:
    void updateOverallState();

    // Active stations
    IText ActiveDeviceT[4];
    ITextVectorProperty ActiveDeviceTP;

    // Stations status
    ILight StationL[4];
    ILightVectorProperty StationLP;

    // Update Period
    INumber UpdatePeriodN[1];
    INumberVectorProperty UpdatePeriodNP;
};

#endif // WEATHERMETA_H