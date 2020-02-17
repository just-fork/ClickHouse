#include <Storages/MergeTree/ReplicatedMergeTreeAltersSequence.h>
#include <cassert>

namespace DB
{

int ReplicatedMergeTreeAltersSequence::getHeadAlterVersion(std::lock_guard<std::mutex> & /*state_lock*/) const
{
    /// If queue empty, than we don't have version
    if (!queue_state.empty())
        return queue_state.begin()->first;
    return -1;
}

void ReplicatedMergeTreeAltersSequence::addMutationForAlter(int alter_version, std::lock_guard<std::mutex> & /*state_lock*/)
{
    /// Metadata alter can be added before, or
    /// maybe already finished if we startup after metadata alter was finished.
    if (!queue_state.count(alter_version))
        queue_state.emplace(alter_version, AlterState{.metadata_finished=true, .data_finished=false});
    else
        queue_state[alter_version].data_finished = false;
}

void ReplicatedMergeTreeAltersSequence::addMetadataAlter(
    int alter_version, bool have_mutation, std::lock_guard<std::mutex> & /*state_lock*/)
{
    if (!queue_state.count(alter_version))
        queue_state.emplace(alter_version, AlterState{.metadata_finished=false, .data_finished=!have_mutation});
    else /// Data alter can be added before.
        queue_state[alter_version].metadata_finished = false;
}

void ReplicatedMergeTreeAltersSequence::finishMetadataAlter(int alter_version, std::unique_lock<std::mutex> & /*state_lock*/)
{
    assert(!queue_state.empty());
    assert(queue_state.begin()->first == alter_version);

    /// If metadata stage finished (or was never added) than we can remove this alter
    if (queue_state[alter_version].data_finished)
        queue_state.erase(alter_version);
    else
        queue_state[alter_version].metadata_finished = true;
}

void ReplicatedMergeTreeAltersSequence::finishDataAlter(int alter_version, std::lock_guard<std::mutex> & /*state_lock*/)
{
    /// queue can be empty after load of finished mutation without move of mutation pointer
    if (queue_state.empty())
        return;

    /// Mutations may finish multiple times (for example, after server restart, before update of mutation pointer)
    if (alter_version >= queue_state.begin()->first)
    {
        /// All alter versions bigger than head have to be present in queue.
        assert(queue_state.count(alter_version));
        if (queue_state[alter_version].metadata_finished)
            queue_state.erase(alter_version);
        else
            queue_state[alter_version].data_finished = true;
    }
}

bool ReplicatedMergeTreeAltersSequence::canExecuteDataAlter(int alter_version, std::lock_guard<std::mutex> & /*state_lock*/) const
{
    if (queue_state.empty())
        return true;

    /// All versions smaller than head, can be executed
    if (alter_version < queue_state.begin()->first)
        return true;

    return queue_state.at(alter_version).metadata_finished;
}

bool ReplicatedMergeTreeAltersSequence::canExecuteMetaAlter(int alter_version, std::lock_guard<std::mutex> & /*state_lock*/) const
{
    if (queue_state.empty())
        return true;

    /// We can execute only alters of metadata which are in head.
    return queue_state.begin()->first == alter_version;
}
}