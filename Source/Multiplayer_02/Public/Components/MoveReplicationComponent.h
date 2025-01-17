// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/CarMovementComponent.h"
#include "MoveReplicationComponent.generated.h"

USTRUCT()
struct FCarPawnState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Car State")
	FTransform Transform;
	UPROPERTY(EditAnywhere, Category = "Car State")
	FVector Velocity;

	UPROPERTY(VisibleAnywhere, Category = "Car State")
	FCarPawnMove LastMove;
};

struct FHermitCubicSpline
{
	FVector StartLocation;
	FVector TargetLocation;
	FVector StartVelocityDerivative;
	FVector TargetVelocityDerivative;

	float VelocityDerivativeTime;

	FVector GetInterpolatedLocation(float LerpRatio) const
	{
		return FMath::CubicInterp(StartLocation,
								  StartVelocityDerivative,
								  TargetLocation,
								  TargetVelocityDerivative,
								  LerpRatio);
	}
	FVector GetInterpolatedVelocity(float LerpRatio) const
	{
		return FMath::CubicInterpDerivative(StartLocation,
											StartVelocityDerivative,
											TargetLocation,
											TargetVelocityDerivative,
											LerpRatio);
	}
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class MULTIPLAYER_02_API UMoveReplicationComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UMoveReplicationComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	
	// Server RPCs
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SendMove(FCarPawnMove Move);

	//
	FCarPawnMove GetServerLastMove() const { return ServerState.LastMove;}
	FString GetEnumRoleString(ENetRole Role);
	void AddMoveToTheQueue(FCarPawnMove Move);

private:
	UCarMovementComponent* MovementComponent;

	// Replicate car state by OnRep EVENT
	UPROPERTY(ReplicatedUsing = OnRep_ServerState)
	FCarPawnState ServerState;
	UFUNCTION() // Replication function calls OnReps
	void OnRep_ServerState();
	void AutonomousProxyOnRep_ServerState();
	void SimulatedProxyOnRep_ServerState();

	TArray<FCarPawnMove> UnacknowledgeMovesArray;
	void RemoveStaleMoves(FCarPawnMove LastMove);
	void UpdateServerState(FCarPawnMove LastMove);
	
	// Move interpolation
	int32 MToCmCoeff = 100;
	float ClientTimeSinceUpdate = 0.0f;
	float ClientTimeBetweenLastUpdates = 0.0f;
	FTransform ClientStartTransform;
	FVector ClientStartVelocity;

	void SimulatedClientTick(float ClientDeltaTime);

	USceneComponent* MeshOffsetComponent = nullptr;

	FHermitCubicSpline CreateSpline();
	void InterpolateLocation(const FHermitCubicSpline& Spline, float LerpRatio);
	void InterpolateVelocity(const FHermitCubicSpline& Spline, float LerpRatio);
	void InterpolateRotation(float LerpRatio);

	// Anti-cheat protection
	float ClientSimulationTime = 0;
};
