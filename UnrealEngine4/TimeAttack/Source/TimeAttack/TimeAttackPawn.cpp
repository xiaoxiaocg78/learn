// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "TimeAttackPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundCue.h"
#include "TimeAttackHud.h"
#include "TimeAttackWheelFront.h"
#include "TimeAttackWheelRear.h"
#include "UObject/ConstructorHelpers.h"
#include "WheeledVehicleMovementComponent4W.h"

// Needed for VR Headset
#if HMD_MODULE_INCLUDED
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IXRTrackingSystem.h"
#endif // HMD_MODULE_INCLUDED

const FName ATimeAttackPawn::LookUpBinding("LookUp");
const FName ATimeAttackPawn::LookRightBinding("LookRight");
const FName ATimeAttackPawn::EngineAudioRPM("RPM");

#define LOCTEXT_NAMESPACE "VehiclePawn"

ATimeAttackPawn::ATimeAttackPawn()
{
    // Car mesh
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> CarMesh(TEXT("/Game/VehicleAdv/Vehicle/Vehicle_SkelMesh.Vehicle_SkelMesh"));
    GetMesh()->SetSkeletalMesh(CarMesh.Object);

    static ConstructorHelpers::FClassFinder<UObject> AnimBPClass(TEXT("/Game/VehicleAdv/Vehicle/VehicleAnimationBlueprint"));
    GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    GetMesh()->SetAnimInstanceClass(AnimBPClass.Class);

    // Setup friction materials
    static ConstructorHelpers::FObjectFinder<UPhysicalMaterial> SlipperyMat(TEXT("/Game/VehicleAdv/PhysicsMaterials/Slippery.Slippery"));
    SlipperyMaterial = SlipperyMat.Object;

    static ConstructorHelpers::FObjectFinder<UPhysicalMaterial> NonSlipperyMat(TEXT("/Game/VehicleAdv/PhysicsMaterials/NonSlippery.NonSlippery"));
    NonSlipperyMaterial = NonSlipperyMat.Object;

    UWheeledVehicleMovementComponent4W* Vehicle4W = CastChecked<UWheeledVehicleMovementComponent4W>(GetVehicleMovement());

    check(Vehicle4W->WheelSetups.Num() == 4);

    // Wheels/Tyres
    // Setup the wheels
    Vehicle4W->WheelSetups[0].WheelClass = UTimeAttackWheelFront::StaticClass();
    Vehicle4W->WheelSetups[0].BoneName = FName("PhysWheel_FL");
    Vehicle4W->WheelSetups[0].AdditionalOffset = FVector(0, -8, 0);

    Vehicle4W->WheelSetups[1].WheelClass = UTimeAttackWheelFront::StaticClass();
    Vehicle4W->WheelSetups[1].BoneName = FName("PhysWheel_FR");
    Vehicle4W->WheelSetups[1].AdditionalOffset = FVector(0, 8, 0);

    Vehicle4W->WheelSetups[2].WheelClass = UTimeAttackWheelRear::StaticClass();
    Vehicle4W->WheelSetups[2].BoneName = FName("PhysWheel_BL");
    Vehicle4W->WheelSetups[2].AdditionalOffset = FVector(0, -8, 0);

    Vehicle4W->WheelSetups[3].WheelClass = UTimeAttackWheelRear::StaticClass();
    Vehicle4W->WheelSetups[3].BoneName = FName("PhysWheel_BR");
    Vehicle4W->WheelSetups[3].AdditionalOffset = FVector(0, 8, 0);

    // Adjust the tire loading
    Vehicle4W->MinNormalizedTireLoad = 0;
    Vehicle4W->MinNormalizedTireLoadFiltered = 0.2;
    Vehicle4W->MaxNormalizedTireLoad = 2;
    Vehicle4W->MaxNormalizedTireLoadFiltered = 2;

    // Engine
    // Torque setup
    Vehicle4W->MaxEngineRPM = 5700;
    Vehicle4W->EngineSetup.TorqueCurve.GetRichCurve()->Reset();
    Vehicle4W->EngineSetup.TorqueCurve.GetRichCurve()->AddKey(0, 400);
    Vehicle4W->EngineSetup.TorqueCurve.GetRichCurve()->AddKey(1890, 500);
    Vehicle4W->EngineSetup.TorqueCurve.GetRichCurve()->AddKey(5730, 400);

    // Adjust the steering
    Vehicle4W->SteeringCurve.GetRichCurve()->Reset();
    Vehicle4W->SteeringCurve.GetRichCurve()->AddKey(0, 1);
    Vehicle4W->SteeringCurve.GetRichCurve()->AddKey(40, 0.7);
    Vehicle4W->SteeringCurve.GetRichCurve()->AddKey(120, 0.6);

    // Transmission
    // We want 4wd
    Vehicle4W->DifferentialSetup.DifferentialType = EVehicleDifferential4W::LimitedSlip_4W;

    // Drive the front wheels a little more than the rear
    Vehicle4W->DifferentialSetup.FrontRearSplit = 0.65;

    // Automatic gearbox
    Vehicle4W->TransmissionSetup.bUseGearAutoBox = true;
    Vehicle4W->TransmissionSetup.GearSwitchTime = 0.15;
    Vehicle4W->TransmissionSetup.GearAutoBoxLatency = 1;

    // Physics settings
    // Adjust the center of mass - the buggy is quite low
    UPrimitiveComponent* UpdatedPrimitive = Cast<UPrimitiveComponent>(Vehicle4W->UpdatedComponent);
    if (UpdatedPrimitive) {
        UpdatedPrimitive->BodyInstance.COMNudge = FVector(8, 0, 0);
    }

    // Set the inertia scale. This controls how the mass of the vehicle is distributed.
    Vehicle4W->InertiaTensorScale = FVector(1, 1.333, 1.2);

    // Create a spring arm component for our chase camera
    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->SetRelativeLocation(FVector(0, 0, 34));
    SpringArm->SetWorldRotation(FRotator(-20, 0, 0));
    SpringArm->SetupAttachment(RootComponent);
    SpringArm->TargetArmLength = 125;
    SpringArm->bEnableCameraLag = false;
    SpringArm->bEnableCameraRotationLag = false;
    SpringArm->bInheritPitch = true;
    SpringArm->bInheritYaw = true;
    SpringArm->bInheritRoll = true;

    // Create the chase camera component
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
    Camera->SetRelativeLocation(FVector(-125.0, 0, 0));
    Camera->SetRelativeRotation(FRotator(10, 0, 0));
    Camera->bUsePawnControlRotation = false;
    Camera->FieldOfView = 90.f;

    // Create In-Car camera component
    InternalCameraOrigin = FVector(-34, -10, 50);
    InternalCameraBase = CreateDefaultSubobject<USceneComponent>(TEXT("InternalCameraBase"));
    InternalCameraBase->SetRelativeLocation(InternalCameraOrigin);
    InternalCameraBase->SetupAttachment(GetMesh());

    InternalCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("InternalCamera"));
    InternalCamera->bUsePawnControlRotation = false;
    InternalCamera->FieldOfView = 90;
    InternalCamera->SetupAttachment(InternalCameraBase);

    // In car HUD
    // Create text render component for in car speed display
    InCarSpeed = CreateDefaultSubobject<UTextRenderComponent>(TEXT("IncarSpeed"));
    InCarSpeed->SetRelativeScale3D(FVector(0.1, 0.1, 0.1));
    InCarSpeed->SetRelativeLocation(FVector(35, -6, 20));
    InCarSpeed->SetRelativeRotation(FRotator(0, 180, 0));
    InCarSpeed->SetupAttachment(GetMesh());

    // Create text render component for in car gear display
    InCarGear = CreateDefaultSubobject<UTextRenderComponent>(TEXT("IncarGear"));
    InCarGear->SetRelativeScale3D(FVector(0.1, 0.1, 0.1));
    InCarGear->SetRelativeLocation(FVector(35, 5, 20));
    InCarGear->SetRelativeRotation(FRotator(0, 180, 0));
    InCarGear->SetupAttachment(GetMesh());

    // Setup the audio component and allocate it a sound cue
    static ConstructorHelpers::FObjectFinder<USoundCue> SoundCue(TEXT("/Game/VehicleAdv/Sound/Engine_Loop_Cue.Engine_Loop_Cue"));
    EngineSoundComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineSound"));
    EngineSoundComponent->SetSound(SoundCue.Object);
    EngineSoundComponent->SetupAttachment(GetMesh());

    // Colors for the in-car gear display. One for normal one for reverse
    GearDisplayReverseColor = FColor(255, 0, 0, 255);
    GearDisplayColor = FColor(255, 255, 255, 255);

    bIsLowFriction = false;
    bInReverseGear = false;
}

void ATimeAttackPawn::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    // set up gameplay key bindings
    check(PlayerInputComponent);

    PlayerInputComponent->BindAxis("MoveForward", this, &ATimeAttackPawn::MoveForward);
    PlayerInputComponent->BindAxis("MoveRight", this, &ATimeAttackPawn::MoveRight);
    PlayerInputComponent->BindAxis(LookUpBinding);
    PlayerInputComponent->BindAxis(LookRightBinding);

    PlayerInputComponent->BindAction("Handbrake", IE_Pressed, this, &ATimeAttackPawn::OnHandbrakePressed);
    PlayerInputComponent->BindAction("Handbrake", IE_Released, this, &ATimeAttackPawn::OnHandbrakeReleased);
    PlayerInputComponent->BindAction("SwitchCamera", IE_Pressed, this, &ATimeAttackPawn::OnToggleCamera);

    PlayerInputComponent->BindAction("ResetVR", IE_Pressed, this, &ATimeAttackPawn::OnResetVR);
}

void ATimeAttackPawn::MoveForward(float Val)
{
    GetVehicleMovementComponent()->SetThrottleInput(Val);
}

void ATimeAttackPawn::MoveRight(float Val)
{
    GetVehicleMovementComponent()->SetSteeringInput(Val);
}

void ATimeAttackPawn::OnHandbrakePressed()
{
    GetVehicleMovementComponent()->SetHandbrakeInput(true);
}

void ATimeAttackPawn::OnHandbrakeReleased()
{
    GetVehicleMovementComponent()->SetHandbrakeInput(false);
}

void ATimeAttackPawn::OnToggleCamera()
{
    EnableIncarView(!bInCarCameraActive);
}

void ATimeAttackPawn::EnableIncarView(const bool bState)
{
    if (bState != bInCarCameraActive) {
        bInCarCameraActive = bState;

        if (bState == true) {
            OnResetVR();
            Camera->Deactivate();
            InternalCamera->Activate();
        } else {
            InternalCamera->Deactivate();
            Camera->Activate();
        }

        InCarSpeed->SetVisibility(bInCarCameraActive);
        InCarGear->SetVisibility(bInCarCameraActive);
    }
}

void ATimeAttackPawn::Tick(float Delta)
{
    Super::Tick(Delta);

    // Setup the flag to say we are in reverse gear
    bInReverseGear = GetVehicleMovement()->GetCurrentGear() < 0;

    // Update phsyics material
    UpdatePhysicsMaterial();

    // Update the strings used in the hud (incar and onscreen)
    UpdateHUDStrings();

    // Set the string in the incar hud
    SetupInCarHUD();

    bool bHMDActive = false;
#if HMD_MODULE_INCLUDED
    if ((GEngine->XRSystem.IsValid() == true) && ((GEngine->XRSystem->IsHeadTrackingAllowed() == true) || (GEngine->IsStereoscopic3D() == true))) {
        bHMDActive = true;
    }
#endif // HMD_MODULE_INCLUDED
    if (bHMDActive == false) {
        if ((InputComponent) && (bInCarCameraActive == true)) {
            FRotator HeadRotation = InternalCamera->RelativeRotation;
            HeadRotation.Pitch += InputComponent->GetAxisValue(LookUpBinding);
            HeadRotation.Yaw += InputComponent->GetAxisValue(LookRightBinding);
            InternalCamera->RelativeRotation = HeadRotation;
        }
    }

    // Pass the engine RPM to the sound component
    float RPMToAudioScale = 2500 / GetVehicleMovement()->GetEngineMaxRotationSpeed();
    EngineSoundComponent->SetFloatParameter(EngineAudioRPM, GetVehicleMovement()->GetEngineRotationSpeed() * RPMToAudioScale);
}

void ATimeAttackPawn::BeginPlay()
{
    Super::BeginPlay();

    bool bWantInCar = false;
    // First disable both speed/gear displays
    bInCarCameraActive = false;
    InCarSpeed->SetVisibility(bInCarCameraActive);
    InCarGear->SetVisibility(bInCarCameraActive);

    // Enable in car view if HMD is attached
#if HMD_MODULE_INCLUDED
    bWantInCar = UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled();
#endif // HMD_MODULE_INCLUDED

    EnableIncarView(bWantInCar);
    // Start an engine sound playing
    EngineSoundComponent->Play();
}

void ATimeAttackPawn::OnResetVR()
{
#if HMD_MODULE_INCLUDED
    if (GEngine->XRSystem.IsValid()) {
        GEngine->XRSystem->ResetOrientationAndPosition();
        InternalCamera->SetRelativeLocation(InternalCameraOrigin);
        GetController()->SetControlRotation(FRotator());
    }
#endif // HMD_MODULE_INCLUDED
}

void ATimeAttackPawn::UpdateHUDStrings()
{
    float KPH = FMath::Abs(GetVehicleMovement()->GetForwardSpeed()) * 0.036f;
    int32 KPH_int = FMath::FloorToInt(KPH);
    int32 Gear = GetVehicleMovement()->GetCurrentGear();

    // Using FText because this is display text that should be localizable
    SpeedDisplayString = FText::Format(LOCTEXT("SpeedFormat", "{0} km/h"), FText::AsNumber(KPH_int));

    if (bInReverseGear == true) {
        GearDisplayString = FText(LOCTEXT("ReverseGear", "R"));
    } else {
        GearDisplayString = (Gear == 0) ? LOCTEXT("N", "N") : FText::AsNumber(Gear);
    }
}

void ATimeAttackPawn::SetupInCarHUD()
{
    APlayerController* PlayerController = Cast<APlayerController>(GetController());
    if ((PlayerController != nullptr) && (InCarSpeed != nullptr) && (InCarGear != nullptr)) {
        // Setup the text render component strings
        InCarSpeed->SetText(SpeedDisplayString);
        InCarGear->SetText(GearDisplayString);

        if (bInReverseGear == false) {
            InCarGear->SetTextRenderColor(GearDisplayColor);
        } else {
            InCarGear->SetTextRenderColor(GearDisplayReverseColor);
        }
    }
}

void ATimeAttackPawn::UpdatePhysicsMaterial()
{
    if (GetActorUpVector().Z < 0) {
        if (bIsLowFriction == true) {
            GetMesh()->SetPhysMaterialOverride(NonSlipperyMaterial);
            bIsLowFriction = false;
        } else {
            GetMesh()->SetPhysMaterialOverride(SlipperyMaterial);
            bIsLowFriction = true;
        }
    }
}

#undef LOCTEXT_NAMESPACE