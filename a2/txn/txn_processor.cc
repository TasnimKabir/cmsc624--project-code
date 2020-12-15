#include "txn/txn_processor.h"
#include <stdio.h>
#include <set>
#include <algorithm>

#include "txn/lock_manager.h"

// Thread & queue counts for StaticThreadPool initialization.
#define THREAD_COUNT 8

TxnProcessor::TxnProcessor(CCMode mode) : mode_(mode), tp_(THREAD_COUNT), next_unique_id_(1)
{
    if (mode_ == LOCKING_EXCLUSIVE_ONLY)
        lm_ = new LockManagerA(&ready_txns_);
    else if (mode_ == LOCKING)
        lm_ = new LockManagerB(&ready_txns_);

    // Create the storage
    if (mode_ == MVCC)
    {
        storage_ = new MVCCStorage();
    }
    else
    {
        storage_ = new Storage();
    }

    storage_->InitStorage();

    // Start 'RunScheduler()' running.
    cpu_set_t cpuset;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    CPU_ZERO(&cpuset);
    for (int i = 0; i < 7; i++)
    {
        CPU_SET(i, &cpuset);
    }
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
    pthread_t scheduler_;
    pthread_create(&scheduler_, &attr, StartScheduler, reinterpret_cast<void*>(this));

    stopped_          = false;
    scheduler_thread_ = scheduler_;
}

void* TxnProcessor::StartScheduler(void* arg)
{
    reinterpret_cast<TxnProcessor*>(arg)->RunScheduler();
    return NULL;
}

TxnProcessor::~TxnProcessor()
{
    // Wait for the scheduler thread to join back before destroying the object and its thread pool.
    stopped_ = true;
    pthread_join(scheduler_thread_, NULL);

    if (mode_ == LOCKING_EXCLUSIVE_ONLY || mode_ == LOCKING) delete lm_;

    delete storage_;
}

void TxnProcessor::NewTxnRequest(Txn* txn)
{
    // Atomically assign the txn a new number and add it to the incoming txn
    // requests queue.
    mutex_.Lock();
    txn->unique_id_ = next_unique_id_;
    next_unique_id_++;
    txn_requests_.Push(txn);
    mutex_.Unlock();
}

Txn* TxnProcessor::GetTxnResult()
{
    Txn* txn;
    while (!txn_results_.Pop(&txn))
    {
        // No result yet. Wait a bit before trying again (to reduce contention on
        // atomic queues).
        usleep(1);
    }
    return txn;
}

void TxnProcessor::RunScheduler()
{
    switch (mode_)
    {
    case SERIAL:
        RunSerialScheduler();
        break;
    case LOCKING:
        RunLockingScheduler();
        break;
    case LOCKING_EXCLUSIVE_ONLY:
        RunLockingScheduler();
        break;
    case OCC:
        RunOCCScheduler();
        break;
    case P_OCC:
        RunOCCParallelScheduler();
        break;
    case MVCC:
        RunMVCCScheduler();
    case STRIFE:
        RunStrife();
    }
}

void TxnProcessor::RunSerialScheduler()
{
    Txn* txn;
    while (!stopped_)
    {
        // Get next txn request.
        if (txn_requests_.Pop(&txn))
        {
            // Execute txn.
            ExecuteTxn(txn);

            // Commit/abort txn according to program logic's commit/abort decision.
            if (txn->Status() == COMPLETED_C)
            {
                ApplyWrites(txn);
                txn->status_ = COMMITTED;
            }
            else if (txn->Status() == COMPLETED_A)
            {
                txn->status_ = ABORTED;
            }
            else
            {
                // Invalid TxnStatus!
                DIE("Completed Txn has invalid TxnStatus: " << txn->Status());
            }

            // Return result to client.
            txn_results_.Push(txn);
        }
    }
}

void TxnProcessor::RunLockingScheduler()
{
    Txn* txn;
    int a;
    a=3;
    while (!stopped_)
    {
        // Start processing the next incoming transaction request.
        if (txn_requests_.Pop(&txn))
        {
            bool blocked = false;
            // Request read locks.
            if (a%2==0)
            {
                for (set<Key>::iterator it = txn->readset_.begin(); it != txn->readset_.end(); ++it)
                {
                    if (!lm_->ReadLock(txn, *it))
                    {
                        blocked = true;
                    }
                }

                // Request write locks.
                for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it)
                {
                    if (!lm_->WriteLock(txn, *it))
                    {
                        blocked = true;
                    }
                }
            }
            else
            {
                for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it)
                {
                    if (!lm_->WriteLock(txn, *it))
                    {
                        blocked = true;
                    }
                }
                for (set<Key>::iterator it = txn->readset_.begin(); it != txn->readset_.end(); ++it)
                {
                    if (!lm_->ReadLock(txn, *it))
                    {
                        blocked = true;
                    }
                }

            }

            // If all read and write locks were immediately acquired, this txn is
            // ready to be executed.
            if (blocked == false)
            {
                ready_txns_.push_back(txn);
            }
        }

        // Process and commit all transactions that have finished running.
        while (completed_txns_.Pop(&txn))
        {
            // Commit/abort txn according to program logic's commit/abort decision.
            if (txn->Status() == COMPLETED_C)
            {
                ApplyWrites(txn);
                txn->status_ = COMMITTED;
            }
            else if (txn->Status() == COMPLETED_A)
            {
                txn->status_ = ABORTED;
            }
            else
            {
                // Invalid TxnStatus!
                DIE("Completed Txn has invalid TxnStatus: " << txn->Status());
            }

            // Release read locks.
            for (set<Key>::iterator it = txn->readset_.begin(); it != txn->readset_.end(); ++it)
            {
                lm_->Release(txn, *it);
            }
            // Release write locks.
            for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it)
            {
                lm_->Release(txn, *it);
            }

            // Return result to client.
            txn_results_.Push(txn);
        }

        // Start executing all transactions that have newly acquired all their
        // locks.
        while (ready_txns_.size())
        {
            // Get next ready txn from the queue.
            txn = ready_txns_.front();
            ready_txns_.pop_front();

            // Start txn running in its own thread.
            tp_.AddTask([this, txn]()
            {
                this->ExecuteTxn(txn);
            });
        }
    }
}

void TxnProcessor::ExecuteTxn(Txn* txn)
{
    // Get the start time
    txn->occ_start_time_ = GetTime();

    // Read everything in from readset.
    for (set<Key>::iterator it = txn->readset_.begin(); it != txn->readset_.end(); ++it)
    {
        // Save each read result iff record exists in storage.
        Value result;
        if (storage_->Read(*it, &result)) txn->reads_[*it] = result;
    }

    // Also read everything in from writeset.
    for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it)
    {
        // Save each read result iff record exists in storage.
        Value result;
        if (storage_->Read(*it, &result)) txn->reads_[*it] = result;
    }

    // Execute txn's program logic.
    txn->Run();

    // Hand the txn back to the RunScheduler thread.
    completed_txns_.Push(txn);
}

void TxnProcessor::ApplyWrites(Txn* txn)
{
    // Write buffered writes out to storage.
    for (map<Key, Value>::iterator it = txn->writes_.begin(); it != txn->writes_.end(); ++it)
    {
        storage_->Write(it->first, it->second, txn->unique_id_);
    }
}

void TxnProcessor::RunOCCScheduler()
{
    //
    // Implement this method!
    //
    // [For now, run serial scheduler in order to make it through the test
    // suite]
    Txn* txn;
    //int count=0;
    while (!stopped_)
    {
        while(txn_requests_.Pop(&txn))
        {
            tp_.AddTask([this, txn]()
            {
                this->ExecuteTxn(txn);
            });
        }
        while (completed_txns_.Pop(&txn))
        {
            if (txn->Status() == COMPLETED_C)
            {
                bool valid=true;
                for (set<Key>::iterator it = txn->readset_.begin(); it != txn->readset_.end(); ++it)
                {
                    double time_updated=storage_->Timestamp(*it);
                    if(txn->occ_start_time_<time_updated)
                    {
                        valid=false;
                    }
                }
                for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it)
                {
                    double time_updated=storage_->Timestamp(*it);
                    if(txn->occ_start_time_<time_updated)
                    {
                        valid=false;
                    }
                }
                if(valid==false)
                {
                    //cleanup txn
                    txn->reads_.clear();
                    txn->writes_.clear();
                    txn->status_ = INCOMPLETE;

                    //Completely restart the transaction
                    mutex_.Lock();
                    txn->unique_id_ = next_unique_id_;
                    next_unique_id_++;
                    txn_requests_.Push(txn);
                    mutex_.Unlock();
                    //std::cout<<"failing transaction\n";
                }
                else
                {
                    //Apply all writes
                    ApplyWrites(txn);
                    //std::cout<<"Committed "<<txn->unique_id_<<"\n"<<std::flush;
                    //count++;
                    //Mark transaction as committed
                    txn->status_=COMMITTED;
                    txn_results_.Push(txn);
                }
            }
            else if (txn->Status() == COMPLETED_A)
            {
                txn->status_ = ABORTED;
                mutex_.Lock();
                txn->unique_id_ = next_unique_id_;
                next_unique_id_++;
                txn_requests_.Push(txn);
                mutex_.Unlock();
            }
            else
            {
                // Invalid TxnStatus!
                DIE("Completed Txn has invalid TxnStatus: " << txn->Status());
                mutex_.Lock();
                txn->unique_id_ = next_unique_id_;
                next_unique_id_++;
                txn_requests_.Push(txn);
                mutex_.Unlock();
                //txn_results_.Push(txn);
            }
        }


    }
    //RunSerialScheduler();
}


void TxnProcessor::ExecuteTxnParallel(Txn* txn)
{
    /* code */
    //std::cout<<"It is calling\n";
    txn->occ_start_time_=GetTime();

    //Read all relevant data from storage
    for (set<Key>::iterator it = txn->readset_.begin(); it != txn->readset_.end(); ++it)
    {
        Value result;
        if (storage_->Read(*it, &result))
            txn->reads_[*it] = result;
    }

    // Also read everything in from writeset.
    for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it)
    {
        Value result;
        if (storage_->Read(*it, &result))
            txn->reads_[*it] = result;
    }

    //Execute the transaction logic (i.e. call Run() on the transaction)
    txn->Run();
    if (txn->Status() == COMPLETED_C)
    {
        active_set_mutex_.Lock();
        set<Txn*> active_set_copy;
        active_set_copy=active_set_.GetSet();
        active_set_.Insert(txn);
        active_set_mutex_.Unlock();

        bool valid=true;
        for (set<Key>::iterator it = txn->readset_.begin(); it != txn->readset_.end(); ++it)
        {
            double time_updated=storage_->Timestamp(*it);
            if(txn->occ_start_time_<time_updated)
            {
                valid=false;
            }
        }
        for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it)
        {
            double time_updated=storage_->Timestamp(*it);
            if(txn->occ_start_time_<time_updated)
            {
                valid=false;
            }
        }
        set <Txn*>:: iterator itr;
        //std::cout<<"\nactive set size: "<<active_set_.Size()<<std::flush;
        for (itr = active_set_copy.begin(); itr != active_set_copy.end(); ++itr)
        {
            Txn* t=*itr;
            set<Key> intersect;
            std::set_intersection(txn->writeset_.begin(),txn->writeset_.end(),t->writeset_.begin(),t->writeset_.end(),std::inserter(intersect,intersect.begin()));
            if(intersect.size()>0)
            {
                valid=false;
                break;
            }

            std::set_intersection(txn->readset_.begin(),txn->readset_.end(),t->writeset_.begin(),t->writeset_.end(),std::inserter(intersect,intersect.begin()));
            //std::cout<<"size of \n"<<intersect.size()<<"\n"<<std::flush;
            if(intersect.size()>0)
            {
                valid=false;
                break;
            }
        }
        if(valid==false)
        {
            active_set_.Erase(txn);
            //cleanup txn
            txn->reads_.clear();
            txn->writes_.clear();
            txn->status_ = INCOMPLETE;

            //Completely restart the transaction
            mutex_.Lock();
            txn->unique_id_ = next_unique_id_;
            next_unique_id_++;
            txn_requests_.Push(txn);
            mutex_.Unlock();
        }
        else
        {
            //Apply all writes
            ApplyWrites(txn);
            active_set_.Erase(txn);
            //Mark transaction as committed
            txn->status_=COMMITTED;
            txn_results_.Push(txn);
        }
    }
    else if (txn->Status() == COMPLETED_A)
    {
        txn->status_ = ABORTED;
        mutex_.Lock();
        txn->unique_id_ = next_unique_id_;
        next_unique_id_++;
        txn_requests_.Push(txn);
        mutex_.Unlock();
    }
    else
    {
        // Invalid TxnStatus!
        DIE("Completed Txn has invalid TxnStatus: " << txn->Status());
        mutex_.Lock();
        txn->unique_id_ = next_unique_id_;
        next_unique_id_++;
        txn_requests_.Push(txn);
        mutex_.Unlock();
        //txn_results_.Push(txn);
    }

}


void TxnProcessor::RunOCCParallelScheduler()
{
    //
    // Implement this method! Note that implementing OCC with parallel
    // validation may need to create another method, like
    // TxnProcessor::ExecuteTxnParallel.
    // Note that you can use active_set_ and active_set_mutex_ we provided
    // for you in the txn_processor.h
    //
    // [For now, run serial scheduler in order to make it through the test
    // suite]
    //RunSerialScheduler();
    //printf("In this function\n");
    Txn* txn;
    while(!stopped_)
    {
        while(txn_requests_.Pop(&txn))
        {
            tp_.AddTask([this, txn]()
            {
                this->ExecuteTxnParallel(txn);
            });

        }
    }
}

void TxnProcessor::MVCCExecuteTxn(Txn* txn)
{
    //Read all necessary data for this transaction from the storage
    for (set<Key>::iterator it = txn->readset_.begin(); it != txn->readset_.end(); ++it)
    {
        // Save each read result iff record exists in storage.
        storage_->Lock(*it);
        Value result;
        if (storage_->Read(*it, &result,txn->unique_id_))
            txn->reads_[*it] = result;
        storage_->Unlock(*it);
    }

    // Also read everything in from writeset.
    for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it)
    {
        // Save each read result iff record exists in storage.
        Value result;
        storage_->Lock(*it);
        if (storage_->Read(*it, &result,txn->unique_id_))
            txn->reads_[*it] = result;
        storage_->Unlock(*it);
    }

    //Execute the transaction logic
    txn->Run();
    //Acquire all the locks for keys in the write_set
    if (txn->Status() == COMPLETED_C)
    {
        for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it)
        {
            storage_->Lock(*it);
        }
        //call MVCCStorage::CheckWrite method to check all the keys in the write_set
        bool valid=true;
        for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it)
        {
            bool check_write=storage_->CheckWrite(*it,txn->unique_id_);
            if(check_write==false)
            {
                valid=false;
                break;
            }
        }
        if(valid==false)
        {
            for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it)
            {
                storage_->Unlock(*it);
            }

            //cleanup txn
            txn->reads_.clear();
            txn->writes_.clear();
            txn->status_ = INCOMPLETE;

            //Completely restart the transaction
            mutex_.Lock();
            txn->unique_id_ = next_unique_id_;
            next_unique_id_++;
            txn_requests_.Push(txn);
            mutex_.Unlock();
        }
        else
        {
            //Apply all writes
            ApplyWrites(txn);

            for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it)
            {
                storage_->Unlock(*it);
            }
            //Mark transaction as committed
            txn->status_=COMMITTED;
            txn_results_.Push(txn);
        }
    }
    else if (txn->Status() == COMPLETED_A)
    {
        txn->status_ = ABORTED;
        mutex_.Lock();
        txn->unique_id_ = next_unique_id_;
        next_unique_id_++;
        txn_requests_.Push(txn);
        mutex_.Unlock();
    }
    else
    {
        // Invalid TxnStatus!
        DIE("Completed Txn has invalid TxnStatus: " << txn->Status());
        mutex_.Lock();
        txn->unique_id_ = next_unique_id_;
        next_unique_id_++;
        txn_requests_.Push(txn);
        mutex_.Unlock();
        //txn_results_.Push(txn);
    }
}



void TxnProcessor::RunMVCCScheduler()
{
    //
    // Implement this method!

    // Hint:Pop a txn from txn_requests_, and pass it to a thread to execute.
    // Note that you may need to create another execute method, like TxnProcessor::MVCCExecuteTxn.
    //
    // [For now, run serial scheduler in order to make it through the test
    // suite]
    Txn* txn;
    while(!stopped_)
    {
        while(txn_requests_.Pop(&txn))
        {
            tp_.AddTask([this, txn]()
            {
                this->MVCCExecuteTxn(txn);
            });

        }
    }
    //RunSerialScheduler();
}

void TxnProcessor::AssignNew()
{
  p=−1;
  q=−1;
  Tnew=txn_requests_.front();
  Tr=queuelist.front();
  while(txn_requests_.Pop(&txn)){
  if(M(Tnew,txn)> p){
    p=M(Tnew,T);
    q=queue(T);
  }
}
  if (p==−1)
  T.enqueue(Tnew);
  else
  Tr.enqueue(Tnew);

}

Record* TxnProcessor::Record* Find(Record* r)
{
    //  first  pass: record  path  and  find  root
    Record* root = r;
    while (root!= root ->parent)   // LP2 (when  false)
    {
        root = root ->parent;
    }
    //  second  pass: path  compression
    CompressPath(r, root);
    return  root;
}
void TxnProcessor::CompressPath(Record* rec, Record* root)
{
    while (rec < root)
    {
        Record* child = rec;
        Record* parent = rec ->parent;
        if (parent  < root)
            cas(&child ->parent, parent, root);
        rec = rec ->parent;
    }
}

void TxnProcessor::RunStrife()
{
    k=100;
    set<Cluster> special;
    int count[k][k];
    for (int i=0;i<k;i++)
    {
      for (int j=0;j<k;j++)
      {
        count[i][j]=0;
      }
    }
    // Spot Step (initially each data node is a cluster)
    int i = 0;
    Txn* txn;
    while(!stopped_)
    {
        for (m=0; m<k; m++)
        {
            txn=txn_requests_.Pop(&txn);
            r=T.nbrs;
            set<Cluster> C = Find(r);
            set<Cluster> S = special;
            if(S.empty())
            {
                c = C.begin();
                while (!C.empty()){
                  others=C.begin();
                  C.erase(others);
                  Union(c, others);
                }
                c.id = i;
                c.count++;
                c.is_special = true;
                special.insert(c);
                i++;
            }
        }
        // Fuse Step
        while(txn_requests_.Pop(&txn)){
        r=T.nbrs
        set<Cluster> C = Find(r);
        set<Cluster> S = special;
        if (S.size()<=1){
        c =S.size();
        C.first=S.first;
        while (!C.empty()){
          others=C.begin();
          C.erase(others);
          Union(c, others);
        }
        c.count++;
        else
            while (!S.empty()){
                t=S.begin();
                c1=t.first();
                c2=t.second();
                count[c1.id][c2.id]++;
              }
        // Merge Step
        while (!S.empty()){
            t=S.begin();
            c1=t.first();
            c2=t.second();
            n1= count[c1.id][c2.id];
          }
        n2=c1.count +c2.count +n1;
        if (n1>=alpha*n2)
        Union(c1,c2);
        // Allocate Step
        while(txn_requests_.Pop(&txn){
        r=T.nbrs
        set<Cluster> C = Find(r);
        if(C.size()=1)
              c = C.begin();
        if(c.queue =0)
               c.queue = new Queue<Txn>();
        worklist.Push(c.queue);
        c.queue.Push(T);
        else
            residuals.Push(T);
        v.worklist=worklist;
        v.residuals=residuals;
        return v

    }
}
}
bool TxnProcessor::Union(Record* r1, Record* r2)
{
    while (true)
    {
        Record* parent = Find(r1);
        Record* child   = Find(r2);
        if (parent  ==  child)
        {
            // do  nothing
            return  true;
        }
        else if (parent  > M and  child  > M)
        {
            // both  are  special
            return  false;
        }
        //  choose  parent , child
        if (parent  < child)
        {
            Record* temp = parent;
            parent = child;
            child = temp;
        }
        //  invariant: parent  > child
        // swap  parent  of  child
        if (cas(&child ->parent, child, parent))     // LP1
        {
            return  true;
        }
    }
}
