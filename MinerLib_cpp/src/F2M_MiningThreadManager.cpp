#include "F2M_MiningThreadManager.h"
#include "F2M_MinerConnection.h"
#include "F2M_WorkThread.h"
#include "F2M_GPUThread.h"
#include "F2M_Work.h"
#include "F2M_Timer.h"

#include <stdio.h>

static F2M_Timer timer;

F2M_MiningThreadManager::F2M_MiningThreadManager(int threadCount, bool useSSE, float gpuPercentage)
{
    mThreadCount = threadCount;
    mThreads = new F2M_WorkThread*[threadCount];
    for( int i = 0; i < threadCount; i++ )
        mThreads[i] = new F2M_WorkThread(useSSE);

    if( gpuPercentage > 0 )
        mGPUThread = new F2M_GPUThread(gpuPercentage);
    else
        mGPUThread = 0;

    mCurrentWork = 0;
}

F2M_MiningThreadManager::~F2M_MiningThreadManager()
{
    if( mGPUThread )
        delete mGPUThread;

    if( mCurrentWork )
        delete mCurrentWork;
}

void F2M_MiningThreadManager::Update(F2M_MinerConnection* connection)
{
    if( mCurrentWork )
    {
        // Doing work, check to see if all threads done
        bool threadsAllDone = true;
        if( mGPUThread )
            threadsAllDone = mGPUThread->IsWorkDone();

        if( threadsAllDone )
        {
            for( int i = 0; i < mThreadCount; i++ )
            {
                if( !mThreads[i]->IsWorkDone() )
                {
                    threadsAllDone = false;
                    break;
                }
            }
        }

        if( threadsAllDone )
        {
            // Get the result and total hashes
            unsigned int hashes = 0;
            bool solutionFound = false;
            unsigned int solution = 0;

            if( mGPUThread )
            {
                solutionFound = mGPUThread->GetSolutionFound();
                solution = mGPUThread->GetSolution();
                hashes += mGPUThread->GetHashesDone();
            }

            for( int i = 0; i < mThreadCount; i++ )
            {
                hashes += mThreads[i]->mHashesDone;
                if( mThreads[i]->mSolutionFound )
                {
                    solutionFound = true;
                    solution = mThreads[i]->mSolution;
                }
            }


            connection->SendWorkComplete(solutionFound, ntohl(solution), hashes);            
            delete mCurrentWork;
            mCurrentWork = 0;
            timer.Stop();
            printf("gpu: %d  hps: %d\n", mGPUThread ? mGPUThread->GetHashrate() : 0, (unsigned int)(hashes / timer.GetDuration()));
        }
    }
}

void F2M_MiningThreadManager::StartWork(F2M_Work* work)
{
    timer.Start();
    if( mCurrentWork )
        delete mCurrentWork;

    mCurrentWork = work;

    unsigned int hashStart = work->hashStart;
    unsigned int hashCount = work->hashCount;
        
    if( mGPUThread )
    {
        // Find out how many hashes the GPU wants to do
        unsigned int gpuHashes = mGPUThread->GetHashrate() * 4;
        if( gpuHashes > hashCount )
            gpuHashes = hashCount / 2;  // GPU wants all of them, limit to half so the server will give us a bigger chunk next time

        // Give the GPU its hashes
        mGPUThread->StartWork(hashStart, gpuHashes, work);
        hashCount -= gpuHashes;
        hashStart += gpuHashes;
    }
    
    unsigned int hashesPerThread = hashCount / mThreadCount;
    for( int i = 0; i < mThreadCount; i++ )
    {
        unsigned int hashes = hashesPerThread;
        if( hashes > hashCount )
            hashes = hashCount;
        hashCount -= hashes;

        mThreads[i]->StartWork(hashStart, hashes, work);
        hashStart += hashes;
    }
}