// Copyright Epic Games, Inc. All Rights Reserved.

#include "WallRunCharacter.h"
#include "WallRunProjectile.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/InputSettings.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "MotionControllerComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPChar, Warning, All);

//////////////////////////////////////////////////////////////////////////
// AWallRunCharacter

AWallRunCharacter::AWallRunCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Create a CameraComponent	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-39.56f, 1.75f, 64.f)); // Position the camera
	FirstPersonCameraComponent->bUsePawnControlRotation = true;

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(FirstPersonCameraComponent);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	Mesh1P->SetRelativeRotation(FRotator(1.9f, -19.19f, 5.2f));
	Mesh1P->SetRelativeLocation(FVector(-0.5f, -4.4f, -155.7f));

	// Create a gun mesh component
	FP_Gun = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FP_Gun"));
	FP_Gun->SetOnlyOwnerSee(false);			// otherwise won't be visible in the multiplayer
	FP_Gun->bCastDynamicShadow = false;
	FP_Gun->CastShadow = false;
	// FP_Gun->SetupAttachment(Mesh1P, TEXT("GripPoint"));
	FP_Gun->SetupAttachment(RootComponent);

	FP_MuzzleLocation = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzleLocation"));
	FP_MuzzleLocation->SetupAttachment(FP_Gun);
	FP_MuzzleLocation->SetRelativeLocation(FVector(0.2f, 48.4f, -10.6f));

	// Default offset from the character location for projectiles to spawn
	GunOffset = FVector(100.0f, 0.0f, 10.0f);

	// Note: The ProjectileClass and the skeletal mesh/anim blueprints for Mesh1P, FP_Gun
	// are set in the derived blueprint asset named MyCharacter to avoid direct content references in C++.
}

void AWallRunCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	//Attach gun mesh component to Skeleton, doing it here because the skeleton is not yet created in the constructor
	FP_Gun->AttachToComponent(Mesh1P, FAttachmentTransformRules(EAttachmentRule::SnapToTarget, true), TEXT("GripPoint"));

	Mesh1P->SetHiddenInGame(false, true);

	GetCapsuleComponent()->OnComponentHit.AddDynamic(this, &AWallRunCharacter::OnPlayerCapsuleHit);

	GetCharacterMovement()->SetPlaneConstraintEnabled(true);

	if (IsValid(CameraTiltCurve))
	{
		FOnTimelineFloat TimelineCallback;
		TimelineCallback.BindUFunction(this, FName("UpdateCameraTilt"));
		CameraTiltTimeline.AddInterpFloat(CameraTiltCurve, TimelineCallback);
	}
}

void AWallRunCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bIsWallRunning)
	{
		UpdateWallRun();
	}
	CameraTiltTimeline.TickTimeline(DeltaSeconds);
}

void AWallRunCharacter::Jump()
{
	if (bIsWallRunning)
	{
		FVector JumpDirection = FVector::ZeroVector;

		if(CurrentWallRunSide == EWallRunSide::Right)
		{
			JumpDirection = FVector::CrossProduct(CurrentDirection, FVector::UpVector).GetSafeNormal();
		}
		else
		{
			JumpDirection = FVector::CrossProduct(FVector::UpVector, CurrentDirection).GetSafeNormal();
		}

		JumpDirection += FVector::UpVector;

		LaunchCharacter(GetCharacterMovement()->JumpZVelocity * JumpDirection.GetSafeNormal(), false, true);
		StopWallRun();
	}
	else
	{
		Super::Jump();
	}
}

//////////////////////////////////////////////////////////////////////////
// Input

void AWallRunCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// set up gameplay key bindings
	check(PlayerInputComponent);

	// Bind jump events
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	// Bind fire event
	PlayerInputComponent->BindAction("Fire", IE_Pressed, this, &AWallRunCharacter::OnFire);

	// Bind movement events
	PlayerInputComponent->BindAxis("MoveForward", this, &AWallRunCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AWallRunCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &AWallRunCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &AWallRunCharacter::LookUpAtRate);
}

void AWallRunCharacter::GetWallRunSideAndDirection(const FVector& HitNormal, EWallRunSide& OutWallRunSide, FVector& OutDirection) const
{
	if (FVector::DotProduct(HitNormal, GetActorRightVector()) > -0.001f)
	{
		OutWallRunSide = EWallRunSide::Left;
		OutDirection = FVector::CrossProduct(HitNormal, FVector::UpVector).GetSafeNormal();
	}
	else
	{
		OutWallRunSide = EWallRunSide::Right;
		OutDirection = FVector::CrossProduct(FVector::UpVector, HitNormal).GetSafeNormal();
	}
}

void AWallRunCharacter::OnPlayerCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bIsWallRunning)
	{
		return;
	}

	FVector HitNormal = Hit.ImpactNormal;

	if (IsSurfaceWallRunnable(HitNormal))
	{
		return;
	}

	if (!GetCharacterMovement()->IsFalling())
	{
		return;
	}

	EWallRunSide WallRunSide = EWallRunSide::None;
	FVector Direction = FVector::ZeroVector;
	GetWallRunSideAndDirection(HitNormal, WallRunSide, Direction);

	if (!AreRequiredKeyDown(WallRunSide))
	{
		return;
	}

	StartWallRun(WallRunSide, Direction);
}

bool AWallRunCharacter::IsSurfaceWallRunnable(const FVector& WallNormal) const
{
	if (WallNormal.Z > GetCharacterMovement()->GetWalkableFloorZ() || WallNormal.Z < 0)
	{
		return true;
	}
	return false;
}

bool AWallRunCharacter::AreRequiredKeyDown(EWallRunSide WallRunSide) const
{
	if (ForwardAxis < 0.001f)
	{
		return false;
	}

	if (RightAxis > -0.001f && WallRunSide == EWallRunSide::Left)
	{
		return false;
	}

	if (RightAxis < 0.001f && WallRunSide == EWallRunSide::Right)
	{
		return false;
	}

	return true;
}

void AWallRunCharacter::StartWallRun(EWallRunSide WallRunSide, FVector Direction)
{
	BeginCameraTilt();

	bIsWallRunning = true;

	CurrentWallRunSide = WallRunSide;
	CurrentDirection = Direction;


	GetCharacterMovement()->SetPlaneConstraintNormal(FVector::UpVector);

	GetWorldTimerManager().SetTimer(WallRunTimerHandle, this, &AWallRunCharacter::StopWallRun, MaxWallRunTime, false);
}

void AWallRunCharacter::StopWallRun()
{
	EndCameraTilt();

	bIsWallRunning = false;

	GetCharacterMovement()->SetPlaneConstraintNormal(FVector::ZeroVector);
}

void AWallRunCharacter::UpdateWallRun()
{
	if (!AreRequiredKeyDown(CurrentWallRunSide))
	{
		StopWallRun();
		return;
	}

	FHitResult HitResult;

	FVector LineTraceDirection = CurrentWallRunSide == EWallRunSide::Right ? GetActorRightVector() : -GetActorRightVector();
	float LineTraceLength = 200.0f;

	FVector StartPosition = GetActorLocation();
	FVector EndPosition = StartPosition + LineTraceLength * LineTraceDirection;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	if (GetWorld()->LineTraceSingleByChannel(HitResult, StartPosition, EndPosition, ECC_Visibility, QueryParams))
	{
		EWallRunSide WallRunSide = EWallRunSide::None;
		FVector Direction = FVector::ZeroVector;
		GetWallRunSideAndDirection(HitResult.ImpactNormal, WallRunSide, Direction);

		if (WallRunSide != CurrentWallRunSide)
		{
			StopWallRun();
		}
		else
		{
			CurrentDirection = Direction;
			GetCharacterMovement()->Velocity = GetCharacterMovement()->GetMaxSpeed() * CurrentDirection;
		}
	}
	else
	{
		StopWallRun();
	}
}

void AWallRunCharacter::UpdateCameraTilt(float Value)
{
	FRotator CurrentControlRotation = GetControlRotation();
	CurrentControlRotation.Roll = CurrentWallRunSide == EWallRunSide::Left ? Value : -Value;
	GetController()->SetControlRotation(CurrentControlRotation);
}

void AWallRunCharacter::OnFire()
{
	// try and fire a projectile
	if (ProjectileClass != nullptr)
	{
		UWorld* const World = GetWorld();
		if (World != nullptr)
		{
			const FRotator SpawnRotation = GetControlRotation();
			// MuzzleOffset is in camera space, so transform it to world space before offsetting from the character location to find the final muzzle position
			const FVector SpawnLocation = ((FP_MuzzleLocation != nullptr) ? FP_MuzzleLocation->GetComponentLocation() : GetActorLocation()) + SpawnRotation.RotateVector(GunOffset);

			//Set Spawn Collision Handling Override
			FActorSpawnParameters ActorSpawnParams;
			ActorSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;

			// spawn the projectile at the muzzle
			World->SpawnActor<AWallRunProjectile>(ProjectileClass, SpawnLocation, SpawnRotation, ActorSpawnParams);
		}
	}

	// try and play the sound if specified
	if (FireSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, GetActorLocation());
	}

	// try and play a firing animation if specified
	if (FireAnimation != nullptr)
	{
		// Get the animation object for the arms mesh
		UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance();
		if (AnimInstance != nullptr)
		{
			AnimInstance->Montage_Play(FireAnimation, 1.f);
		}
	}
}

void AWallRunCharacter::MoveForward(float Value)
{
	ForwardAxis = Value;
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorForwardVector(), Value);
	}
}

void AWallRunCharacter::MoveRight(float Value)
{
	RightAxis = Value;
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorRightVector(), Value);
	}
}

void AWallRunCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void AWallRunCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}
