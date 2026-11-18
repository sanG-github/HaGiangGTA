#include "HGInputFactory.h"

#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace HGInput
{
	static UEnhancedInputLocalPlayerSubsystem* GetSubsystem(const APawn* Pawn)
	{
		if (!Pawn)
		{
			return nullptr;
		}
		const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
		if (!PC || !PC->IsLocalController())
		{
			return nullptr;
		}
		const ULocalPlayer* LP = PC->GetLocalPlayer();
		return LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	}

	UInputAction* MakeAction(UObject* Outer, FName Name, EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name, RF_Transient);
		Action->ValueType = ValueType;
		return Action;
	}

	UInputMappingContext* MakeContext(UObject* Outer, FName Name)
	{
		return NewObject<UInputMappingContext>(Outer, Name, RF_Transient);
	}

	void MapKey(UInputMappingContext* Context, UInputAction* Action, const FKey& Key, bool bNegate, bool bSwizzleYX)
	{
		if (!Context || !Action)
		{
			return;
		}
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		if (bSwizzleYX)
		{
			UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Context, NAME_None, RF_Transient);
			Swizzle->Order = EInputAxisSwizzle::YXZ;
			Mapping.Modifiers.Add(Swizzle);
		}
		if (bNegate)
		{
			UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(Context, NAME_None, RF_Transient);
			Mapping.Modifiers.Add(Negate);
		}
	}

	void AddContext(const APawn* Pawn, UInputMappingContext* Context, int32 Priority)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetSubsystem(Pawn))
		{
			if (Context)
			{
				// AddMappingContext ignores duplicates (it only updates the priority).
				Subsystem->AddMappingContext(Context, Priority);
			}
		}
	}

	void RemoveContext(const APawn* Pawn, UInputMappingContext* Context)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetSubsystem(Pawn))
		{
			if (Context)
			{
				Subsystem->RemoveMappingContext(Context);
			}
		}
	}
}
