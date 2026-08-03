// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "CarWheelFront.h"

UCarWheelFront::UCarWheelFront()
{
    WheelRadius = 38.f;
    WheelWidth = 17.0f;
    bAffectedByHandbrake = false;
    bAffectedBySteering = true;
    MaxSteerAngle = 50.f;
    AxleType = EAxleType::Front;

    // Setup suspension forces
    SuspensionForceOffset = FVector(0.0f, 0.0f, 0.0f);
    SuspensionMaxRaise = 10.0f;
    SuspensionMaxDrop = 10.0f;
    SuspensionDampingRatio = 1.5f;
}
