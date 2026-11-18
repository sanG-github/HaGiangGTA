#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"

class UInputAction;
class UInputMappingContext;
class APawn;
struct FKey;

/**
 * Builds Enhanced Input actions / mapping contexts at runtime so the project
 * needs no binary input .uasset files (code-first, git friendly).
 */
namespace HGInput
{
	/** Create a transient input action owned by Outer. */
	HAGIANGGTA_API UInputAction* MakeAction(UObject* Outer, FName Name, EInputActionValueType ValueType);

	/** Create a transient, empty mapping context owned by Outer. */
	HAGIANGGTA_API UInputMappingContext* MakeContext(UObject* Outer, FName Name);

	/**
	 * Map Key -> Action inside Context.
	 * @param bNegate   multiply the value by -1 (e.g. S for throttle, A for steer)
	 * @param bSwizzleYX route a 1D key onto the Y axis of a 2D action
	 */
	HAGIANGGTA_API void MapKey(UInputMappingContext* Context, UInputAction* Action, const FKey& Key, bool bNegate = false, bool bSwizzleYX = false);

	/** Add / remove a mapping context on the local player that controls Pawn. */
	HAGIANGGTA_API void AddContext(const APawn* Pawn, UInputMappingContext* Context, int32 Priority = 0);
	HAGIANGGTA_API void RemoveContext(const APawn* Pawn, UInputMappingContext* Context);
}
