#pragma once

#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"


// Delegate to notify when the best move is found (thread-safe)
DECLARE_DELEGATE_TwoParams(FOnStockfishResult, const FString& /*BestMove*/, bool /*bIsWhite*/);

// The runnable class to manage Stockfish process in a separate thread
class FStockfishRunnable : public FRunnable
{
public:
    // Constructor
    FStockfishRunnable(const FString& InFEN, int32 InThreads, FOnStockfishResult InCallback, bool bInIsWhite, int32 InDepth, int32 InSkillLevel)
        : FEN(InFEN)
        , Threads(InThreads)
        , Callback(InCallback)
        , bIsWhite(bInIsWhite)
        , StockfishHandle()
        , StockfishInputPipeRead(nullptr)
        , StockfishInputPipeWrite(nullptr)
        , StockfishOutputPipeRead(nullptr)
        , StockfishOutputPipeWrite(nullptr)
        , bStopThread(false)
        , Depth(InDepth)
        , SkillLevel(InSkillLevel)
    {}

    virtual ~FStockfishRunnable()
    {
        StopProcess();
        UE_LOG(LogTemp, Log, TEXT("StockfishRunnable destroyed."));
    }

    virtual bool Init() override { return true; }

    virtual uint32 Run() override
    {
        //UE_LOG(LogTemp, Log, TEXT("StockfishRunnable started on thread %s"), *FPlatformTLS::GetCurrentThreadId());

        if (!StartStockfish()) return 0;

        if (!SendUCIInit()) return 0;

        RequestBestMove();

        StopProcess();

        UE_LOG(LogTemp, Log, TEXT("StockfishRunnable finished."));
        return 0;
    }

    virtual void Stop() override
    {
        bStopThread = true;
        UE_LOG(LogTemp, Log, TEXT("StockfishRunnable received stop signal."));

        if (StockfishInputPipeWrite)
        {
            FString QuitCmd = TEXT("quit\n");
            FTCHARToUTF8 QuitUTF8(*QuitCmd);
            FPlatformProcess::WritePipe(StockfishInputPipeWrite, reinterpret_cast<const uint8*>(QuitUTF8.Get()), QuitUTF8.Length(), nullptr);
            UE_LOG(LogTemp, Log, TEXT("Sent 'quit' to Stockfish."));
        }
    }

private:
    std::atomic<bool> bStopThread;

    FString FEN;
    int32 Threads;
    bool bIsWhite;
    int32 Depth;
    int32 SkillLevel;

    FOnStockfishResult Callback;

    FProcHandle StockfishHandle;
    void* StockfishInputPipeRead;
    void* StockfishInputPipeWrite;
    void* StockfishOutputPipeRead;
    void* StockfishOutputPipeWrite;

    bool StartStockfish()
    {
        FString StockfishPath = FPaths::ProjectContentDir() + TEXT("ThirdParty/Stockfish/stockfish.exe");
        UE_LOG(LogTemp, Log, TEXT("Attempting to start Stockfish at path: %s"), *StockfishPath);

        if (!FPaths::FileExists(StockfishPath))
        {
            UE_LOG(LogTemp, Error, TEXT("Stockfish executable not found!"));
            return false;
        }

        // Create pipes
        if (!FPlatformProcess::CreatePipe(StockfishOutputPipeRead, StockfishOutputPipeWrite, false) ||
            !FPlatformProcess::CreatePipe(StockfishInputPipeRead, StockfishInputPipeWrite, true))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create Stockfish pipes."));
            return false;
        }

        StockfishHandle = FPlatformProcess::CreateProc(
            *StockfishPath,
            TEXT(""),
            true, true, true,
            nullptr, 0,
            nullptr,
            StockfishOutputPipeWrite,
            StockfishInputPipeRead
        );

        if (!StockfishHandle.IsValid())
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to launch Stockfish process."));
            return false;
        }

        UE_LOG(LogTemp, Log, TEXT("Stockfish process started successfully."));
        return true;
    }

    bool SendUCIInit()
    {
        UE_LOG(LogTemp, Log, TEXT("Initializing UCI with Stockfish..."));
        WriteLine("uci");
        WriteLine(FString::Printf(TEXT("setoption name Threads value %d"), Threads));
        WriteLine(FString::Printf(TEXT("setoption name Skill Level value %d"),
            FMath::Clamp(SkillLevel, 0, 20)));
        WriteLine("isready");

        const float Timeout = 5.0f;
        float TimeWaited = 0.0f;

        while (!bStopThread)
        {
            FString Output = ReadLine();
            if (!Output.IsEmpty())
            {
                UE_LOG(LogTemp, Log, TEXT("Stockfish output: %s"), *Output);
            }

            if (Output.Contains("readyok"))
            {
                UE_LOG(LogTemp, Log, TEXT("Stockfish is ready."));
                return true;
            }

            FPlatformProcess::Sleep(0.05f);
            TimeWaited += 0.05f;

            if (TimeWaited >= Timeout)
            {
                UE_LOG(LogTemp, Warning, TEXT("Stockfish init timeout after %.2f seconds."), TimeWaited);
                return false;
            }
        }

        return false;
    }

    void RequestBestMove()
    {
        if (bStopThread) return;
    
        UE_LOG(LogTemp, Log, TEXT("Requesting best move for FEN: %s, depth: %d, player white: %s"), *FEN, Depth, bIsWhite ? TEXT("true") : TEXT("false"));
    
        // Send FEN and depth command to Stockfish
        WriteLine(FString::Printf(TEXT("position fen %s"), *FEN));
        WriteLine(FString::Printf(TEXT("go depth %d"), Depth));
    
        const float TimeoutLimit = 30.0f; 
        float TimeElapsed = 0.0f;
    
        // Buffer to store incomplete output
        FString OutputBuffer;
    
        while (!bStopThread)
        {
            FString Chunk = ReadLine();
            if (!Chunk.IsEmpty())
            {
                OutputBuffer += Chunk;
    
                // Split the buffer into lines
                TArray<FString> Lines;
                OutputBuffer.ParseIntoArrayLines(Lines, true);
    
                // Keep the last line in the buffer if it might be incomplete
                OutputBuffer = Lines.Num() > 0 && !OutputBuffer.EndsWith("\n") ? Lines.Last() : "";
    
                for (int32 i = 0; i < Lines.Num(); ++i)
                {
                    const FString& Line = (i == Lines.Num() - 1 && !OutputBuffer.IsEmpty()) ? "" : Lines[i]; // Skip incomplete last line
                    if (Line.IsEmpty()) continue;
    
                    UE_LOG(LogTemp, Log, TEXT("Stockfish output line: %s"), *Line);
    
                    // Check for best move
                    if (Line.StartsWith("bestmove"))
                    {
                        TArray<FString> Tokens;
                        Line.ParseIntoArray(Tokens, TEXT(" "));
                        if (Tokens.Num() >= 2)
                        {
                            FString BestMove = Tokens[1];
                            UE_LOG(LogTemp, Log, TEXT("Best move found: %s"), *BestMove);
                            
                            bool bLocalIsWhite = bIsWhite;
                            AsyncTask(ENamedThreads::GameThread, [Callback = Callback, BestMove, bLocalIsWhite]()
                            {
                                Callback.ExecuteIfBound(BestMove, bLocalIsWhite);
                            });
                            return;
                        }
                    }
                }
            }
            else
            {
                FPlatformProcess::Sleep(0.05f);
                TimeElapsed += 0.05f;
    
                if (TimeElapsed >= TimeoutLimit)
                {
                    UE_LOG(LogTemp, Warning, TEXT("Stockfish move calculation timed out after %.2f seconds."), TimeElapsed);
                    break;
                }
            }
        }
    UE_LOG(LogTemp, Warning, TEXT("No best move found for FEN: %s"), *FEN);
    }

    void StopProcess()
    {
        UE_LOG(LogTemp, Log, TEXT("Stopping Stockfish process..."));

        // Terminate the Stockfish process if it's valid
        if (StockfishHandle.IsValid())
        {
            if (FPlatformProcess::IsProcRunning(StockfishHandle))
            {
                FPlatformProcess::TerminateProc(StockfishHandle, true);
            }
            FPlatformProcess::CloseProc(StockfishHandle);
        }

        // Close pipes safely
        if (StockfishInputPipeRead || StockfishInputPipeWrite)
        {
            FPlatformProcess::ClosePipe(StockfishInputPipeRead, StockfishInputPipeWrite);
            StockfishInputPipeRead = nullptr;
            StockfishInputPipeWrite = nullptr;
        }

        if (StockfishOutputPipeRead || StockfishOutputPipeWrite)
        {
            FPlatformProcess::ClosePipe(StockfishOutputPipeRead, StockfishOutputPipeWrite);
            StockfishOutputPipeRead = nullptr;
            StockfishOutputPipeWrite = nullptr;
        }

        UE_LOG(LogTemp, Log, TEXT("Stockfish process stopped and pipes closed."));
    }

    void WriteLine(const FString& Line)
    {
        if (!StockfishInputPipeWrite || bStopThread)
            return;

        FString LineWithNewline = Line + TEXT("\n");
        FTCHARToUTF8 UTF8Line(*LineWithNewline);

        bool bSuccess = FPlatformProcess::WritePipe(
            StockfishInputPipeWrite, 
            reinterpret_cast<const uint8*>(UTF8Line.Get()), 
            UTF8Line.Length(), 
            nullptr
        );

        if (!bSuccess)
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to write to Stockfish pipe"));
        }
    }

    FString ReadLine()
    {
        if (!StockfishOutputPipeRead || bStopThread)
            return FString();
    
        return FPlatformProcess::ReadPipe(StockfishOutputPipeRead);
    }
};