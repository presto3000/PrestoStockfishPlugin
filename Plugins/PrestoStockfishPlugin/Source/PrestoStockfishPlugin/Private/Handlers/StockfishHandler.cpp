// Copyright 2024 presto3000, All Rights Reserved.


#include "Handlers/StockfishHandler.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadManager.h"
#include "Runnables/StockfishRunnable.h"



UStockfishHandler::UStockfishHandler()
{
    StockfishRunnable = nullptr;
    StockfishThread = nullptr;
    bStopThread = false;
}

void UStockfishHandler::BeginDestroy()
{
    CleanupStockfishThread();
    Super::BeginDestroy();
}

void UStockfishHandler::CleanupStockfishThread()
{
    if (StockfishRunnable)
    {
        StockfishRunnable->Stop();
    }

    if (StockfishThread)
    {
        StockfishThread->WaitForCompletion();
        delete StockfishThread;
        StockfishThread = nullptr;
    }

    if (StockfishRunnable)
    {
        delete StockfishRunnable;
        StockfishRunnable = nullptr;
    }

    bStopThread = false;
    UE_LOG(LogTemp, Log, TEXT("Stockfish thread cleaned up."));
}

void UStockfishHandler::RequestBestMove(const FString& FEN, bool bIsWhite, int32 Depth, int32 SkillLevel)
{
    // Stop any running thread
    CleanupStockfishThread();

    // Bind the callback, marshaling to game thread
    FOnStockfishResult LocalCallback = FOnStockfishResult::CreateLambda(
        [WeakThis = TWeakObjectPtr<UStockfishHandler>(this)](const FString& BestMove, bool bPlayerWhite, float Eval, const FString& EvalText)
        {
            if (!WeakThis.IsValid()) return;

            AsyncTask(ENamedThreads::GameThread, [WeakThis, BestMove, bPlayerWhite, Eval, EvalText]()
            {
                if (WeakThis.IsValid())
                {
                    WeakThis->OnBestMoveFound.Broadcast(BestMove, bPlayerWhite, Eval, EvalText);
                }
            });
        }
    );
    
    // the maximum number of threads CPU can safely handle
    int32 MaxThreads = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
    //int32 ThreadsToUse = FMath::Clamp(MaxThreads, 1, MaxThreads);
    int32 ThreadsToUse = FPlatformMisc::NumberOfCores() - 2;
    
    StockfishRunnable = new FStockfishRunnable(FEN, ThreadsToUse, LocalCallback, bIsWhite, Depth, SkillLevel);
    StockfishThread = FRunnableThread::Create(StockfishRunnable, TEXT("StockfishThread"));

    UE_LOG(LogTemp, Log, TEXT("Stockfish thread started for SkillLevel %d, depth %d"), SkillLevel, Depth);
}