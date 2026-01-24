#pragma once

#include "CoreMinimal.h"
#include "Runnables/StockfishRunnable.h"
#include "UObject/Object.h"

#include "StockfishHandler.generated.h"

// Delegate to notify when Stockfish finds a move
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStockfishMoveFound, const FString&, BestMove, bool, bIsWhite);
class FStockfishRunnable;

UCLASS(Blueprintable, BlueprintType)
class PRESTOSTOCKFISHPLUGIN_API UStockfishHandler : public UObject
{
	GENERATED_BODY()

public:
	UStockfishHandler();
	virtual void BeginDestroy() override;
	
	UFUNCTION(BlueprintCallable, Category = "Chess")
	void RequestBestMove(const FString& FEN, bool bIsWhite, int32 Depth, int32 SkillLevel);
	
	UPROPERTY(BlueprintAssignable, Category = "Chess")
	FOnStockfishMoveFound OnBestMoveFound;

private:
	FStockfishRunnable* StockfishRunnable;
	FRunnableThread* StockfishThread;
	FThreadSafeBool bStopThread;

	void CleanupStockfishThread();
};