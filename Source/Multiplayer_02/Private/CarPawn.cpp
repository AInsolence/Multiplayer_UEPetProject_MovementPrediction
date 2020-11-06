// Fill out your copyright notice in the Description page of Project Settings.


#include "CarPawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/BoxComponent.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"

// Sets default values
ACarPawn::ACarPawn()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	// Make acrtor be replicated
	bReplicates = true;

	// Create mesh component
	CollisionBox = CreateDefaultSubobject<UBoxComponent>("CollisionBox");
	SetRootComponent(CollisionBox);
	// Create mesh component
	CarMesh = CreateDefaultSubobject<USkeletalMeshComponent>("CarMesh");
	// Create spring arm for the main camera
	SpringArm = CreateDefaultSubobject<USpringArmComponent>("SpringArm");
	// Create main camera
	Camera_01 = CreateDefaultSubobject<UCameraComponent>("Camera_01");

	// Set components hierarcy attachment
	if (ensureMsgf(CollisionBox, TEXT("CollisionBox component is not found")))
	{
		// Set physics
		CollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		CollisionBox->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);

		if (ensureMsgf(CarMesh, TEXT("Mesh component is not found")))
		{
			CarMesh->SetupAttachment(CollisionBox);

			if (ensureMsgf(SpringArm, TEXT("SpringArm component is not found")))
			{
				SpringArm->TargetArmLength = 600.f;
				// Turn spring arm under the ground
				SpringArm->SetRelativeRotation(FQuat::MakeFromEuler(FVector(0, -25, 0)));
				SpringArm->SetupAttachment(CarMesh);

				if (ensureMsgf(Camera_01, TEXT("Camera_01 component is not found")))
				{
					Camera_01->FieldOfView = 90.f;
					Camera_01->SetupAttachment(SpringArm);
					Camera_01->Activate(true);
				}
			}
		}
	}
}

void ACarPawn::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACarPawn, ReplicatedTransform);
	DOREPLIFETIME(ACarPawn, Velocity);
	DOREPLIFETIME(ACarPawn, Throttle);
	DOREPLIFETIME(ACarPawn, SteeringThrow);
}

// Called when the game starts or when spawned
void ACarPawn::BeginPlay()
{
	Super::BeginPlay();
	NetUpdateFrequency = 1.0f;
}

// Called every frame
void ACarPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// Driving
	UpdateLocationFormVelocity(DeltaTime);
	// Steering
	ApplyRotation(DeltaTime);
	// Show Net Role
	DrawDebugString(GetWorld(),
					FVector(0, 0, 100), 
					GetEnumRoleString(this->GetLocalRole()), 
					this, 
					FColor::White, 
					DeltaTime);
	// Set actor transform on the server 
	if (HasAuthority())
	{
		ReplicatedTransform = GetActorTransform();
	}
}

void ACarPawn::OnRep_ReplicatedTransform()
{// Replicate actors transform on the client if on the server it was changed
	SetActorTransform(ReplicatedTransform);
}

void ACarPawn::ApplyRotation(float DeltaTime)
{
	// Find turn angle in this frame
	float DeltaLocation = FVector::DotProduct(GetActorForwardVector(), Velocity) * DeltaTime;
	float TurnAngle = DeltaLocation/SteeringRadius * SteeringThrow;
	FQuat TurnRotation(GetActorUpVector(), TurnAngle);
	AddActorWorldRotation(TurnRotation);
	// Turn car velocity vector
	Velocity = TurnRotation.RotateVector(Velocity);
}

void ACarPawn::UpdateLocationFormVelocity(float DeltaTime)
{
	// Find driving force
	DrivingForce = GetActorForwardVector() * EnginePowerInNewtons * Throttle;
	// Find and apply air resistance and rolling resistance forces
	DrivingForce += GetAirResistance();
	DrivingForce += GetRollingResistance();
	// Find acceleration
	FVector Acceleration = DrivingForce / MassOfTheCarInKg;
	// Find velocity
	Velocity += Acceleration * DeltaTime;
	// Find distance traveled in this frame
	FVector DistansPerFrame = Velocity * DeltaTime * 100;
	// Change car position
	FHitResult HitResult;
	AddActorWorldOffset(DistansPerFrame, true, &HitResult);
	// Check physical blocking
	if (HitResult.IsValidBlockingHit())
	{
		Velocity = FVector::ZeroVector;
	}
}

FVector ACarPawn::GetAirResistance()
{
	return -Velocity.GetSafeNormal() * FMath::Square(Velocity.Size()) * CarDragCoefficient;
}

FVector ACarPawn::GetRollingResistance()
{
	float AccelerationDueToGravity = - GetWorld()->GetGravityZ()/100 * MassOfTheCarInKg;
	FVector NormalForce = - Velocity.GetSafeNormal() * RollingResistanceCoefficient * AccelerationDueToGravity;
	return NormalForce;
}

FString ACarPawn::GetEnumRoleString(ENetRole LocalRole)
{
	switch (LocalRole)
	{
		case ROLE_None:
			return "None";
			break;
		case ROLE_SimulatedProxy:
			return "SimulatedProxy";
			break;
		case ROLE_AutonomousProxy:
			return "AutonomousProxy";
			break;
		case ROLE_Authority:
			return "Authority";
			break;
		case ROLE_MAX:
			return "MAX";
			break;
		default:
			return "ERROR";
			break;
	}
}

// Called to bind functionality to input
void ACarPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void ACarPawn::MoveForward(float Value)
{
	Throttle = Value;
	Server_MoveForward(Value);
}

void ACarPawn::Server_MoveForward_Implementation(float Value)
{
	Throttle = Value;
}

bool ACarPawn::Server_MoveForward_Validate(float Value)
{
	return FMath::Abs(Value) <= 1.f;
}


void ACarPawn::MoveRight(float Value)
{
	SteeringThrow = Value;
	Server_MoveRight(Value);
}

void ACarPawn::Server_MoveRight_Implementation(float Value)
{
	SteeringThrow = Value;
}

bool ACarPawn::Server_MoveRight_Validate(float Value)
{
	return FMath::Abs(Value) <= 1.f;
}