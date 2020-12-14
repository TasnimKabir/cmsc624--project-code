// Lock manager implementing deterministic two-phase locking as described in
// 'The Case for Determinism in Database Systems'.

#include "txn/lock_manager.h"

LockManagerA::LockManagerA(deque<Txn*>* ready_txns)
{
    ready_txns_ = ready_txns;
}
bool LockManagerA::WriteLock(Txn* txn, const Key& key)
{
    //
    // Implement this method!
    unordered_map<Key, deque<LockRequest>*>::iterator iter = lock_table_.find(key);
    //std::cerr<<"\nEntered in writelock function 2nd line\n";
    if (iter != lock_table_.end() )
    {
        deque<LockRequest>* d=iter->second;
        if(d->size()==0)
        {
            enum LockMode mode;
            mode=EXCLUSIVE;
            LockRequest lr(mode,txn);
            d->push_back(lr);
            lock_table_.insert(std::pair<Key, deque<LockRequest>*>(key,d));
            return true;
        }
        else
        {
            LockRequest lr=d->at(0);
            if(lr.txn_==txn)
            {
                return true;
            }
            deque <LockRequest>:: iterator it;
            int flag=1;
            for (it = d->begin(); it != d->end(); ++it)
            {
                if(it->txn_==txn)
                    flag=0;
            }
            if(flag==1)
            {
                enum LockMode mode;
                mode=EXCLUSIVE;
                LockRequest lr(mode,txn);
                d->push_back(lr);

                unordered_map<Txn*, int>::iterator iter2 = txn_waits_.find(txn);
                if (iter2 != txn_waits_.end() )
                {
                    //std::cerr<<"\nEntered here for txn waits:\n";
                    int pending_lock=iter2->second;
                    txn_waits_[txn]=pending_lock+1;
                }
                else
                {
                    //std::cerr<<"\n\nEntered else here for txn waits:\n";
                    txn_waits_.insert({txn,1});
                }
            }
            //std::cerr<<"\nqueue size for lock"<<d->size()<<"\n\n";
            return false;
        }
    }
    else
    {
        //std::cerr<<"lock tble size: "<<lock_table_.size()<<"\n";
        enum LockMode mode;
        mode=EXCLUSIVE;
        LockRequest lr(mode,txn);
        deque<LockRequest>* d=new deque<LockRequest>();
        d->push_back(lr);
        //std::cerr<<"\npushed in queue\n";
        lock_table_.insert(std::pair<Key, deque<LockRequest>*>(key,d) );
    }

    return true;
}

bool LockManagerA::ReadLock(Txn* txn, const Key& key)
{
    // Since Part 1A implements ONLY exclusive locks, calls to ReadLock can
    // simply use the same logic as 'WriteLock'.
    return WriteLock(txn, key);
}

void LockManagerA::Release(Txn* txn, const Key& key)
{
    //
    // Implement this method!
    unordered_map<Key, deque<LockRequest>*>::iterator iter = lock_table_.find(key);
    //std::cerr<<"\nEntered in release function"<<ready_txns_->size()<<"\n";
    if (iter != lock_table_.end() )
    {
        deque<LockRequest>* d=iter->second;
        if(d->size()>=1)
        {
            LockRequest lr=d->at(0);
            if(lr.txn_==txn && d->size()>=2)
            {
                LockRequest next_lr=d->at(1);
                Txn* next_txn=next_lr.txn_;
                unordered_map<Txn*, int>::iterator iter2 = txn_waits_.find(next_txn);
                if (iter2 != txn_waits_.end() )
                {
                    //std::cerr<<"\n\nIn ready transaction process before if\n\n";
                    int pending_lock=iter2->second;
                    if(pending_lock==1)
                    {
                        //std::cerr<<"\n\nIn ready transaction process\n\n";
                        txn_waits_.erase(next_txn);
                        ready_txns_->push_back(next_txn);
                    }
                    else
                    {
                        txn_waits_[next_txn]=pending_lock-1;
                    }
                }
            }


            deque <LockRequest>:: iterator it;
            for (it = d->begin(); it != d->end(); ++it)
            {
                if(it->txn_==txn)
                {
                    d->erase(it);
                    break;
                }
            }
        }
        unordered_map<Txn*, int>:: iterator it2 = txn_waits_.find(txn);
        if (it2 != txn_waits_.end() )
        {
            txn_waits_.erase(it2);
        }

    }

}

// NOTE: The owners input vector is NOT assumed to be empty.
LockMode LockManagerA::Status(const Key& key, vector<Txn*>* owners)
{
    //
    // Implement this method!
    unordered_map<Key, deque<LockRequest>*>::iterator iter = lock_table_.find(key);
    //std::cerr<<"owner size"<<owners->size();
    owners->clear();
    //std::cerr<<"\nEntered in status function with key "<<key<<"\n";
    if (iter != lock_table_.end() )
    {

        //std::cerr<<"\nfound key\n";
        deque<LockRequest>* k=iter->second;
        //std::cerr<<"\nreturned true in status main function "<<k->size()<<"\n";
        LockRequest lr=k->at(0);
        owners->push_back(lr.txn_);
        //std::cerr<<"owner size"<<owners->size();
        return EXCLUSIVE;
    }
    else
        return UNLOCKED;
    //return UNLOCKED;
}

LockManagerB::LockManagerB(deque<Txn*>* ready_txns)
{
    ready_txns_ = ready_txns;
}
bool LockManagerB::WriteLock(Txn* txn, const Key& key)
{
    //
    // Implement this method!
    unordered_map<Key, deque<LockRequest>*>::iterator iter = lock_table_.find(key);
    //std::cerr<<"\nEntered in writelock function 2nd line\n";
    if (iter != lock_table_.end() )
    {
        deque<LockRequest>* d=iter->second;
        if(d->size()==0)
        {
            enum LockMode mode;
            mode=EXCLUSIVE;
            LockRequest lr(mode,txn);
            d->push_back(lr);
            lock_table_.insert(std::pair<Key, deque<LockRequest>*>(key,d));
            return true;
        }
        else
        {
            LockRequest lr=d->at(0);
            if(lr.txn_==txn)
            {
                return true;
            }
            deque <LockRequest>:: iterator it;
            int flag=1;
            for (it = d->begin(); it != d->end(); ++it)
            {
                if(it->txn_==txn)
                    flag=0;
            }
            if(flag==1)
            {
                enum LockMode mode;
                mode=EXCLUSIVE;
                LockRequest lr(mode,txn);
                d->push_back(lr);

                unordered_map<Txn*, int>::iterator iter2 = txn_waits_.find(txn);
                if (iter2 != txn_waits_.end() )
                {
                    //std::cerr<<"\nEntered here for txn waits:\n";
                    int pending_lock=iter2->second;
                    txn_waits_[txn]=pending_lock+1;
                }
                else
                {
                    //std::cerr<<"\n\nEntered else here for txn waits:\n";
                    txn_waits_.insert({txn,1});
                }
            }
            //std::cerr<<"\nqueue size for lock"<<d->size()<<"\n\n";
            return false;
        }
    }
    else
    {
        //std::cerr<<"lock tble size: "<<lock_table_.size()<<"\n";
        enum LockMode mode;
        mode=EXCLUSIVE;
        LockRequest lr(mode,txn);
        deque<LockRequest>* d=new deque<LockRequest>();
        d->push_back(lr);
        //std::cerr<<"\npushed in queue\n";
        lock_table_.insert(std::pair<Key, deque<LockRequest>*>(key,d) );
    }
    //return true;
    return true;
}

bool LockManagerB::ReadLock(Txn* txn, const Key& key)
{
    //
    // Implement this method!
    unordered_map<Key, deque<LockRequest>*>::iterator iter = lock_table_.find(key);
    //std::cerr<<"\nEntered in writelock function 2nd line\n";
    if (iter != lock_table_.end() )
    {
        deque<LockRequest>* d=iter->second;
        if(d->size()==0)
        {
            enum LockMode mode;
            mode=SHARED;
            LockRequest lr(mode,txn);
            d->push_back(lr);
            lock_table_.insert(std::pair<Key, deque<LockRequest>*>(key,d));
            return true;
        }
        else
        {
            LockRequest lr=d->at(0);
            if(lr.txn_==txn)
            {
                return true;
            }
            deque <LockRequest>:: iterator it;
            int shared=1;
            for (it = d->begin(); it != d->end(); ++it)
            {
                if(it->mode_==EXCLUSIVE)
                {
                    shared=0;
                }
            }
            if(shared==1)
            {
                enum LockMode mode;
                mode=SHARED;
                LockRequest lr(mode,txn);
                d->push_back(lr);
                lock_table_.insert(std::pair<Key, deque<LockRequest>*>(key,d));
                return true;
            }
            int flag=1;
            for (it = d->begin(); it != d->end(); ++it)
            {
                if(it->txn_==txn)
                    flag=0;
            }
            if(flag==1)
            {
                enum LockMode mode;
                mode=SHARED;
                LockRequest lr(mode,txn);
                d->push_back(lr);

                unordered_map<Txn*, int>::iterator iter2 = txn_waits_.find(txn);
                if (iter2 != txn_waits_.end() )
                {
                    //std::cerr<<"\nEntered here for txn waits:\n";
                    int pending_lock=iter2->second;
                    txn_waits_[txn]=pending_lock+1;
                }
                else
                {
                    //std::cerr<<"\n\nEntered else here for txn waits:\n";
                    txn_waits_.insert({txn,1});
                }
            }
            //std::cerr<<"\nqueue size for lock"<<d->size()<<"\n\n";
            return false;
        }
    }
    else
    {
        //std::cerr<<"lock tble size: "<<lock_table_.size()<<"\n";
        enum LockMode mode;
        mode=SHARED;
        LockRequest lr(mode,txn);
        deque<LockRequest>* d=new deque<LockRequest>();
        d->push_back(lr);
        //std::cerr<<"\npushed in queue\n";
        lock_table_.insert(std::pair<Key, deque<LockRequest>*>(key,d) );
    }

    return true;
}

void LockManagerB::Release(Txn* txn, const Key& key)
{
    //
    // Implement this method!
    unordered_map<Key, deque<LockRequest>*>::iterator iter = lock_table_.find(key);
    //std::cerr<<"\nEntered in release function"<<ready_txns_->size()<<"\n";
    if (iter != lock_table_.end() )
    {
        deque<LockRequest>* d=iter->second;
        if(d->size()>=1)
        {
            LockRequest lr=d->at(0);
            if(lr.txn_==txn && d->size()>=2)
            {
                LockRequest next_lr=d->at(1);
                if(next_lr.mode_==EXCLUSIVE)
                {
                    Txn* next_txn=next_lr.txn_;
                    unordered_map<Txn*, int>::iterator iter2 = txn_waits_.find(next_txn);
                    if (iter2 != txn_waits_.end() )
                    {
                        //std::cerr<<"\n\nIn ready transaction process before if\n\n";
                        int pending_lock=iter2->second;
                        if(pending_lock==1)
                        {
                            //std::cerr<<"\n\nIn ready transaction process\n\n";
                            txn_waits_.erase(next_txn);
                            ready_txns_->push_back(next_txn);
                        }
                        else
                        {
                            txn_waits_[next_txn]=pending_lock-1;
                        }
                    }
                }
                else if (next_lr.mode_==SHARED)
                {
                    if(lr.mode_==EXCLUSIVE)
                    {
                        //std::cerr<<"\nIn this case failing!!!"<<ready_txns_->size()<<"\n";
                        int j=1;
                        //deque <LockRequest>:: iterator it;
                        //for (it = d->begin(); it != d->end(); ++it)
                        int size=d->size();
                        while(j<size)
                        {
                            LockRequest lr2=d->at(j);
                            if(lr2.mode_==EXCLUSIVE)
                            {
                                break;
                            }
                            Txn* next_txn2=lr2.txn_;
                            unordered_map<Txn*, int>::iterator iter2 = txn_waits_.find(next_txn2);
                            if (iter2 != txn_waits_.end() )
                            {
                                //std::cerr<<"\n\nIn ready transaction process before if\n\n";
                                int pending_lock=iter2->second;
                                if(pending_lock==1)
                                {
                                    //std::cerr<<"\n\nIn ready transaction process\n\n";
                                    txn_waits_.erase(next_txn2);
                                    ready_txns_->push_back(next_txn2);
                                }
                                else
                                {
                                    txn_waits_[next_txn2]=pending_lock-1;
                                }
                            }
                            j++;
                        }
                    }
                }
            }
            else if(lr.txn_!=txn)
            {
                int i=0;
                deque <LockRequest>:: iterator it;
                for (it = d->begin(); it != d->end(); ++it)
                {
                    LockRequest lr1=d->at(i);
                    if(lr1.txn_==txn && lr1.mode_==EXCLUSIVE)
                    {
                        int j=i+1;
                        int size=d->size();
                        while (j<size)
                        {
                            LockRequest lr2=d->at(j);
                            if(lr2.mode_==EXCLUSIVE)
                            {
                                break;
                            }
                            Txn* next_txn2=lr2.txn_;
                            unordered_map<Txn*, int>::iterator iter2 = txn_waits_.find(next_txn2);
                            if (iter2 != txn_waits_.end() )
                            {
                                //std::cerr<<"\n\nIn ready transaction process before if\n\n";
                                int pending_lock=iter2->second;
                                if(pending_lock==1)
                                {
                                    //std::cerr<<"\n\nIn ready transaction process\n\n";
                                    txn_waits_.erase(next_txn2);
                                    ready_txns_->push_back(next_txn2);
                                }
                                else
                                {
                                    txn_waits_[next_txn2]=pending_lock-1;
                                }
                            }
                            j++;
                        }
                    }
                    else if(lr1.txn_!=txn && lr1.mode_==EXCLUSIVE)
                    {
                        break;
                    }
                    i++;
                }
            }
        }


        deque <LockRequest>:: iterator it;
        for (it = d->begin(); it != d->end(); ++it)
        {
            if(it->txn_==txn)
            {
                d->erase(it);
                break;
            }
        }
    }
    unordered_map<Txn*, int>:: iterator it2 = txn_waits_.find(txn);
    if (it2 != txn_waits_.end() )
    {
        txn_waits_.erase(it2);
    }
}

// NOTE: The owners input vector is NOT assumed to be empty.
LockMode LockManagerB::Status(const Key& key, vector<Txn*>* owners)
{
    //
    // Implement this method!
    unordered_map<Key, deque<LockRequest>*>::iterator iter = lock_table_.find(key);
    //std::cerr<<"owner size"<<owners->size();
    owners->clear();
    int flag=2;
    //std::cerr<<"\nEntered in status function with key "<<key<<"\n";
    if (iter != lock_table_.end() )
    {
        deque<LockRequest>* k=iter->second;
        LockRequest lr=k->at(0);
        if(lr.mode_==EXCLUSIVE)
        {
            flag=0;
            owners->push_back(lr.txn_);
        }
        else if(lr.mode_==SHARED)
        {
            flag=1;
            int i=0;
            deque <LockRequest>:: iterator it;
            for (it = k->begin(); it != k->end(); ++it)
            {
                LockRequest lr1=k->at(i);
                if(lr1.mode_==EXCLUSIVE)
                    break;
                owners->push_back(lr1.txn_);
                i++;
            }
        }
        //std::cerr<<"\nreturned true in status main function "<<k->size()<<"\n";
    }
    if(flag==0)
        return EXCLUSIVE;
    else if (flag==1)
        return SHARED;
    else
        return UNLOCKED;

    //return UNLOCKED;
}
