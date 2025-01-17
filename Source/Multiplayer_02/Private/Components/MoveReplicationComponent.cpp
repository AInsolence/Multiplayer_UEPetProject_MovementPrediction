// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/MoveReplicationComponent.h"
#include "Net/UnrealNetwork.h"

// Sets default values for this component's properties
UMoveReplicationComponent::UMoveReplicationComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
}


// Called when the game starts
void UMoveReplicationComponent::BeginPlay()
{
	Super::BeginPlay();
	MovementComponent = GetOwner()->FindComponentByClass<UCarMovementComponent>();
	auto MeshOffsetObject = GetOwner()->GetDefaultSubobjectByName("MeshOffsetComponet");
	if (MeshOffsetObject)
	{
		MeshOffsetComponent = Cast<USceneComponent>(MeshOffsetObject);
	}
}


// Called every frame
void UMoveReplicationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!ensureMsgf(MovementComponent, TEXT("MovementComponent is not found")))
	{
		return;
	}
	// Get last move to send with RPC and check it on the server side
	auto LastMove = MovementComponent->GetLastMove();

	auto ControlledPawn = Cast<APawn>(GetOwner());
	if (!ensureMsgf(ControlledPawn, TEXT("Inside the movement component Pawn is not found!")))
	{
		return;
	}
	// Update Listen-Client state on the server for simulation purposes
	if (GetOwnerRole() == ROLE_Authority && ControlledPawn->IsLocallyControlled())
	{
		AddMoveToTheQueue(LastMove);
		UpdateServerState(LastMove);
	}
	// 
	if (GetOwnerRole() == ROLE_AutonomousProxy)
	{
		/* Add move to the queue and send it to the server (!!!) where
		   it would be simulated as Server-side ("Canonical" simulation) code */
		AddMoveToTheQueue(LastMove);
		Server_SendMove(LastMove);
	}

	if (GetOwnerRole() == ROLE_SimulatedProxy)
	{
		SimulatedClientTick(DeltaTime);
	}
}

void UMoveReplicationComponent::SimulatedClientTick(float ClientDeltaTime)
{
	ClientTimeSinceUpdate += ClientDeltaTime;

	if (ClientTimeBetweenLastUpdates < KINDA_SMALL_NUMBER)
	{
		return;
	}

	if (!ensureMsgf(MovementComponent, TEXT("SimulatedClientTick: MovementComponent is not found")))
	{
		return;
	}

	// Find Lerp ratio based on server update time
	auto LerpRatio = ClientTimeSinceUpdate / ClientTimeBetweenLastUpdates;
	
	// Create interpolation spline
	FHermitCubicSpline Spline = CreateSpline();
	
	InterpolateLocation(Spline, LerpRatio);
	InterpolateVelocity(Spline, LerpRatio);
	InterpolateRotation(LerpRatio);
}

FHermitCubicSpline UMoveReplicationComponent::CreateSpline()
{
	FHermitCubicSpline Spline;

	Spline.StartLocation = ClientStartTransform.GetLocation();
	Spline.TargetLocation = ServerState.Transform.GetLocation();
	Spline.StartVelocityDerivative = ClientStartVelocity * ClientTimeBetweenLastUpdates * MToCmCoeff;
	Spline.TargetVelocityDerivative = ServerState.Velocity * ClientTimeBetweenLastUpdates * MToCmCoeff;

	return Spline;
}

void UMoveReplicationComponent::InterpolateLocation(const FHermitCubicSpline& Spline, float LerpRatio)
{
	auto NextLocation = Spline.GetInterpolatedLocation(LerpRatio);
	// Move only mesh into the interpolated position
	if (MeshOffsetComponent)
	{
		MeshOffsetComponent->SetWorldLocation(NextLocation);
	}
}

void UMoveReplicationComponent::InterpolateVelocity(const FHermitCubicSpline& Spline, float LerpRatio)
{
	// Find an interpolated velocity derivative
	FVector VelocityDerivative = Spline.GetInterpolatedVelocity(LerpRatio);
	FVector NextVelocity = VelocityDerivative / (ClientTimeBetweenLastUpdates * MToCmCoeff);
	// Set a new velocity
	MovementComponent->SetVelocity(NextVelocity);
}

void UMoveReplicationComponent::InterpolateRotation(float LerpRatio)
{
	auto TargetRotation = ServerState.Transform.GetRotation();
	auto NextRotation = FQuat::Slerp(ClientStartTransform.GetRotation(), TargetRotation, LerpRatio);
	// Apply interpolated rotation only to the mesh
	if (MeshOffsetComponent)
	{
		MeshOffsetComponent->SetWorldRotation(NextRotation);
	}
}

void UMoveReplicationComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMoveReplicationComponent, ServerState);
}

void UMoveReplicationComponent::AddMoveToTheQueue(FCarPawnMove Move)
{
	UnacknowledgeMovesArray.Add(Move);
}

void UMoveReplicationComponent::RemoveStaleMoves(FCarPawnMove LastMove)
{
	TArray<FCarPawnMove> NewMoves;
	for (const FCarPawnMove& Move : UnacknowledgeMovesArray)
	{
		if (Move.TimeOfExecuting > LastMove.TimeOfExecuting)
		{
			NewMoves.Add(Move);
		}
	}
	UnacknowledgeMovesArray = NewMoves;
}

void UMoveReplicationComponent::OnRep_ServerState()
{
	switch (GetOwnerRole())
	{
		case ROLE_AutonomousProxy:
			AutonomousProxyOnRep_ServerState();
			break;
		case ROLE_SimulatedProxy:
			SimulatedProxyOnRep_ServerState();
			break;
		default:
			break;
	}
}

void UMoveReplicationComponent::AutonomousProxyOnRep_ServerState()
{
	if (!ensureMsgf(MovementComponent, TEXT("MovementComponent is not found")))
	{
		return;
	}
	// Replicate actors state on the client if on the server it was changed
	GetOwner()->SetActorTransform(ServerState.Transform);
	MovementComponent->SetVelocity(ServerState.Velocity);
	// Update unacknowledged moves
	RemoveStaleMoves(ServerState.LastMove);
	// Reproduce all moves after receiving the server move state
	for (const FCarPawnMove& Move : UnacknowledgeMovesArray)
	{
		MovementComponent->SimulateMove(Move);
	}
}

void UMoveReplicationComponent::SimulatedProxyOnRep_ServerState()
{
	if (!ensureMsgf(MovementComponent, TEXT("SimulatedProxyOnRep_ServerState: MovementComponent is not found")))
	{
		return;
	}

	// Update simulation variables
	ClientTimeBetweenLastUpdates = ClientTimeSinceUpdate;
	ClientTimeSinceUpdate = 0.0f;
	ClientStartVelocity = MovementComponent->GetVelocity();
	if (MeshOffsetComponent != nullptr)
	{
		ClientStartTransform.SetLocation(MeshOffsetComponent->GetComponentLocation());
		ClientStartTransform.SetRotation(MeshOffsetComponent->GetComponentQuat());
	}
	
	// Set right collider position
	GetOwner()->SetActorTransform(ServerState.Transform);
}
void UMoveReplicationComponent::Server_SendMove_Implementation(FCarPawnMove Move)
{
	if (!ensureMsgf(MovementComponent, TEXT("MovementComponent is not found")))
	{
		return;
	}
	// Update client time for anti-cheat
	ClientSimulationTime += Move.DeltaTime;
	// Simulate move on the server ("Canonical simulation)
	MovementComponent->SimulateMove(Move);
	// Save the canonical state on the server
	UpdateServerState(Move);
}

void UMoveReplicationComponent::UpdateServerState(FCarPawnMove LastMove)
{
	ServerState.Transform = GetOwner()->GetActorTransform();
	ServerState.Velocity = MovementComponent->GetVelocity();
	ServerState.LastMove = LastMove;
}

bool UMoveReplicationComponent::Server_SendMove_Validate(FCarPawnMove Move)
{
	// Check client's input
	if (!Move.IsInputValid())
	{
		UE_LOG(LogTemp, Error, TEXT("User try to use input cheat"));
		return false;
	}

	// Check client's delta time proposition
	float ProposedTime = ClientSimulationTime + Move.DeltaTime;
	if (ProposedTime > GetWorld()->TimeSeconds)
	{
		UE_LOG(LogTemp, Error, TEXT("User use cheat and try to run faster than server"));
		return false;
	}

	return true;
}

FString UMoveReplicationComponent::GetEnumRoleString(ENetRole LocalRole)
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