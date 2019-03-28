/***************************************************************************
 *   Copyright (C) 2017 by Nicolas Carion                                  *
 *   This file is part of Kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3 or any later version accepted by the       *
 *   membership of KDE e.V. (or its successor approved  by the membership  *
 *   of KDE e.V.), which shall act as a proxy defined in Section 14 of     *
 *   version 3 of the license.                                             *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifndef TIMELINEMODEL_H
#define TIMELINEMODEL_H

#include "definitions.h"
#include "undohelper.hpp"
#include <QAbstractItemModel>
#include <QReadWriteLock>
#include <cassert>
#include <memory>
#include <mlt++/MltTractor.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class AssetParameterModel;
class EffectStackModel;
class ClipModel;
class CompositionModel;
class DocUndoStack;
class GroupsModel;
class SnapModel;
class TimelineItemModel;
class TrackModel;

/* @brief This class represents a Timeline object, as viewed by the backend.
   In general, the Gui associated with it will send modification queries (such as resize or move), and this class authorize them or not depending on the
   validity of the modifications.

   This class also serves to keep track of all objects. It holds pointers to all tracks and clips, and gives them unique IDs on creation. These Ids are used in
   any interactions with the objects and have nothing to do with Melt IDs.

   This is the entry point for any modifications that has to be made on an element. The dataflow beyond this entry point may vary, for example when the user
   request a clip resize, the call is deferred to the clip itself, that check if there is enough data to extend by the requested amount, compute the new in and
   out, and then asks the track if there is enough room for extension. To avoid any confusion on which function to call first, rembember to always call the
   version in timeline. This is also required to generate the Undo/Redo operators

   The undo/redo system is designed around lambda functions. Each time a function executes an elementary change to the model, it writes the corresponding
   operation and its reverse, respectively in the redo and the undo lambdas. This way, if an operation fails for some reason, we can easily cancel the steps
   that have been done so far without corrupting anything. The other advantage is that operations are easy to compose, and you get a undo/redo pair for free no
   matter in which way you combine them.

   Most of the modification functions are named requestObjectAction. Eg, if the object is a clip and we want to move it, we call requestClipMove. These
   functions always return a bool indicating success, and when they return false they should guarantee than nothing has been modified. Most of the time, these
   functions come in two versions: the first one is the entry point if you want to perform only the action (and not compose it with other actions). This version
   will generally automatically push and Undo object on the Application stack, in case the user later wants to cancel the operation. It also generally goes the
   extra mile to ensure the operation is done in a way that match the user's expectation: for example requestClipMove checks whether the clip belongs to a group
   and in that case actually mouves the full group. The other version of the function, if it exists, is intended for composition (using the action as part of a
   complex operation). It takes as input the undo/redo lambda corresponding to the action that is being performed and accumulates on them. Note that this
   version does the minimal job: in the example of the requestClipMove, it will not move the full group if the clip is in a group.

   Generally speaking, we don't check ahead of time if an action is going to succeed or not before applying it.
   We just apply it naively, and if it fails at some point, we use the undo operator that we are constructing on the fly to revert what we have done so far.
   For example, when we move a group of clips, we apply the move operation to all the clips inside this group (in the right order). If none fails, we are good,
   otherwise we revert what we've already done.
   This kind of behaviour frees us from the burden of simulating the actions before actually applying theme. This is a good thing because this simulation step
   would be very sensitive to corruptions and small discrepancies, which we try to avoid at all cost.


   It derives from AbstractItemModel (indirectly through TimelineItemModel) to provide the model to the QML interface. An itemModel is organized with row and
   columns that contain the data. It can be hierarchical, meaning that a given index (row,column) can contain another level of rows and column.
   Our organization is as follows: at the top level, each row contains a track. These rows are in the same order as in the actual timeline.
   Then each of this row contains itself sub-rows that correspond to the clips.
   Here the order of these sub-rows is unrelated to the chronological order of the clips,
   but correspond to their Id order. For example, if you have three clips, with ids 12, 45 and 150, they will receive row index 0,1 and 2.
   This is because the order actually doesn't matter since the clips are rendered based on their positions rather than their row order.
   The id order has been chosen because it is consistent with a valid ordering of the clips.
   The columns are never used, so the data is always in column 0

   An ModelIndex in the ItemModel consists of a row number, a column number, and a parent index. In our case, tracks have always an empty parent, and the clip
   have a track index as parent.
   A ModelIndex can also store one additional integer, and we exploit this feature to store the unique ID of the object it corresponds to.

*/
class TimelineModel : public QAbstractItemModel_shared_from_this<TimelineModel>
{
    Q_OBJECT

protected:
    /* @brief this constructor should not be called. Call the static construct instead
     */
    TimelineModel(Mlt::Profile *profile, std::weak_ptr<DocUndoStack> undo_stack);

public:
    friend class TrackModel;
    template <typename T> friend class MoveableItem;
    friend class ClipModel;
    friend class CompositionModel;
    friend class GroupsModel;
    friend class TimelineController;
    friend struct TimelineFunctions;

    /// Two level model: tracks and clips on track
    enum {
        NameRole = Qt::UserRole + 1,
        ResourceRole, /// clip only
        ServiceRole,  /// clip only
        StartRole,    /// clip only
        BinIdRole,    /// clip only
        TrackIdRole,
        FakeTrackIdRole,
        FakePositionRole,
        MarkersRole, /// clip only
        StatusRole,  /// clip only
        TypeRole,    /// clip only
        KeyframesRole,
        DurationRole,
        InPointRole,    /// clip only
        OutPointRole,   /// clip only
        FramerateRole,  /// clip only
        GroupedRole,    /// clip only
        HasAudio,       /// clip only
        CanBeAudioRole, /// clip only
        CanBeVideoRole, /// clip only
        IsDisabledRole, /// track only
        IsAudioRole,
        SortRole,
        ShowKeyframesRole,
        AudioLevelsRole,   /// clip only
        AudioChannelsRole, /// clip only
        IsCompositeRole,   /// track only
        IsLockedRole,      /// track only
        HeightRole,        /// track only
        TrackTagRole,      /// track only
        FadeInRole,        /// clip only
        FadeOutRole,       /// clip only
        FileHashRole,      /// clip only
        SpeedRole,         /// clip only
        ReloadThumbRole,   /// clip only
        ItemATrack,        /// composition only
        ItemIdRole,
        ThumbsFormatRole,   /// track only
        EffectNamesRole,    // track and clip only
        EffectsEnabledRole, // track and clip only
        GrabbedRole,        /// clip+composition only
        TrackActiveRole,    /// track only
        AudioRecordRole     /// track only
    };

    ~TimelineModel() override;
    Mlt::Tractor *tractor() const { return m_tractor.get(); }
    /* @brief Load tracks from the current tractor, used on project opening
     */
    void loadTractor();

    /* @brief Returns the current tractor's producer, useful fo control seeking, playing, etc
     */
    std::shared_ptr<Mlt::Producer> producer();
    Mlt::Profile *getProfile();

    /* @brief returns the number of tracks */
    int getTracksCount() const;

    /* @brief returns the track index (id) from its position */
    int getTrackIndexFromPosition(int pos) const;

    /* @brief returns the track index (id) from its position */
    Q_INVOKABLE bool isAudioTrack(int trackId) const;

    /* @brief returns the number of clips */
    int getClipsCount() const;

    /* @brief returns the number of compositions */
    int getCompositionsCount() const;

    /* @brief Returns the id of the track containing clip (-1 if it is not inserted)
       @param clipId Id of the clip to test */
    Q_INVOKABLE int getClipTrackId(int clipId) const;

    /* @brief Returns the id of the track containing composition (-1 if it is not inserted)
       @param clipId Id of the composition to test */
    Q_INVOKABLE int getCompositionTrackId(int compoId) const;

    /* @brief Convenience function that calls either of the previous ones based on item type*/
    Q_INVOKABLE int getItemTrackId(int itemId) const;

    Q_INVOKABLE int getCompositionPosition(int compoId) const;
    int getCompositionPlaytime(int compoId) const;

    /* Returns an item position, item can be clip or composition */
    Q_INVOKABLE int getItemPosition(int itemId) const;
    /* Returns an item duration, item can be clip or composition */
    int getItemPlaytime(int itemId) const;

    /* Returns the current speed of a clip */
    double getClipSpeed(int clipId) const;

    /* @brief Helper function to query the amount of free space around a clip
     * @param clipId: the queried clip. If it is not inserted on a track, this functions returns 0
     * @param after: if true, we return the blank after the clip, otherwise, before.
     */
    int getBlankSizeNearClip(int clipId, bool after) const;

    /* @brief if the clip belongs to a AVSplit group, then return the id of the other corresponding clip. Otherwise, returns -1 */
    int getClipSplitPartner(int clipId) const;

    /* @brief Helper function that returns true if the given ID corresponds to a clip */
    Q_INVOKABLE bool isClip(int id) const;

    /* @brief Helper function that returns true if the given ID corresponds to a composition */
    Q_INVOKABLE bool isComposition(int id) const;

    /* @brief Helper function that returns true if the given ID corresponds to a timeline item (composition or clip) */
    Q_INVOKABLE bool isItem(int id) const;

    /* @brief Helper function that returns true if the given ID corresponds to a track */
    Q_INVOKABLE bool isTrack(int id) const;

    /* @brief Helper function that returns true if the given ID corresponds to a group */
    Q_INVOKABLE bool isGroup(int id) const;

    /* @brief Given a composition Id, returns its underlying parameter model */
    std::shared_ptr<AssetParameterModel> getCompositionParameterModel(int compoId) const;
    /* @brief Given a clip Id, returns its underlying effect stack model */
    std::shared_ptr<EffectStackModel> getClipEffectStackModel(int clipId) const;

    /* @brief Returns the position of clip (-1 if it is not inserted)
       @param clipId Id of the clip to test
    */
    Q_INVOKABLE int getClipPosition(int clipId) const;
    Q_INVOKABLE bool addClipEffect(int clipId, const QString &effectId, bool notify = true);
    Q_INVOKABLE bool addTrackEffect(int trackId, const QString &effectId);
    bool removeFade(int clipId, bool fromStart);
    Q_INVOKABLE bool copyClipEffect(int clipId, const QString &sourceId);
    Q_INVOKABLE bool copyTrackEffect(int trackId, const QString &sourceId);
    bool adjustEffectLength(int clipId, const QString &effectId, int duration, int initialDuration);

    /* @brief Returns the closest snap point within snapDistance
     */
    Q_INVOKABLE int suggestSnapPoint(int pos, int snapDistance);

    /** @brief Return the previous track of same type as source trackId, or trackId if no track found */
    Q_INVOKABLE int getPreviousTrackId(int trackId);
    /** @brief Return the next track of same type as source trackId, or trackId if no track found */
    Q_INVOKABLE int getNextTrackId(int trackId);

    /* @brief Returns the in cut position of a clip
       @param clipId Id of the clip to test
    */
    int getClipIn(int clipId) const;

    /* @brief Returns the clip state (audio/video only)
     */
    PlaylistState::ClipState getClipState(int clipId) const;

    /* @brief Returns the bin id of the clip master
       @param clipId Id of the clip to test
    */
    const QString getClipBinId(int clipId) const;

    /* @brief Returns the duration of a clip
       @param clipId Id of the clip to test
    */
    int getClipPlaytime(int clipId) const;

    /* @brief Returns the size of the clip's frame (widthxheight)
       @param clipId Id of the clip to test
    */
    QSize getClipFrameSize(int clipId) const;
    /* @brief Returns the number of clips in a given track
       @param trackId Id of the track to test
    */
    int getTrackClipsCount(int trackId) const;

    /* @brief Returns the number of compositions in a given track
       @param trackId Id of the track to test
    */
    int getTrackCompositionsCount(int trackId) const;

    /* @brief Returns the position of the track in the order of the tracks
       @param trackId Id of the track to test
    */
    int getTrackPosition(int trackId) const;

    /* @brief Returns the track's index in terms of mlt's internal representation
     */
    int getTrackMltIndex(int trackId) const;
    /* @brief Returns a sort position for tracks.
     * @param separated: if true, the tracks will be sorted like: V2,V1,A1,A2
     * Otherwise, the tracks will be sorted like V2,A2,V1,A1
     */
    int getTrackSortValue(int trackId, bool separated) const;

    /* @brief Returns the ids of the tracks below the given track in the order of the tracks
       Returns an empty list if no track available
       @param trackId Id of the track to test
    */
    QList<int> getLowerTracksId(int trackId, TrackType type = TrackType::AnyTrack) const;

    /* @brief Returns the MLT track index of the video track just below the given track
       @param trackId Id of the track to test
    */
    int getPreviousVideoTrackPos(int trackId) const;
    /* @brief Returns the Track id of the video track just below the given track
       @param trackId Id of the track to test
    */
    int getPreviousVideoTrackIndex(int trackId) const;

    /* @brief Returns the Id of the corresponding audio track. If trackId corresponds to video1, this will return audio 1 and so on */
    int getMirrorAudioTrackId(int trackId) const;
    int getMirrorVideoTrackId(int trackId) const;
    int getMirrorTrackId(int trackId) const;

    /* @brief Move a clip to a specific position
       This action is undoable
       Returns true on success. If it fails, nothing is modified.
       If the clip is not in inserted in a track yet, it gets inserted for the first time.
       If the clip is in a group, the call is deferred to requestGroupMove
       @param clipId is the ID of the clip
       @param trackId is the ID of the target track
       @param position is the position where we want to move
       @param updateView if set to false, no signal is sent to qml
       @param logUndo if set to false, no undo object is stored
    */
    Q_INVOKABLE bool requestClipMove(int clipId, int trackId, int position, bool updateView = true, bool logUndo = true, bool invalidateTimeline = false);

    /* @brief Move a composition to a specific position This action is undoable
       Returns true on success. If it fails, nothing is modified. If the clip is
       not in inserted in a track yet, it gets inserted for the first time. If
       the clip is in a group, the call is deferred to requestGroupMove @param
       transid is the ID of the composition @param trackId is the ID of the
       track */
    Q_INVOKABLE bool requestCompositionMove(int compoId, int trackId, int position, bool updateView = true, bool logUndo = true);

    /* Same function, but accumulates undo and redo, and doesn't check
       for group*/
    bool requestClipMove(int clipId, int trackId, int position, bool updateView, bool invalidateTimeline, Fun &undo, Fun &redo);
    bool requestCompositionMove(int transid, int trackId, int compositionTrack, int position, bool updateView, bool finalMove, Fun &undo, Fun &redo);

    /* When timeline edit mode is insert or overwrite, we fake the move (as it will overlap existing clips, and only process the real move on drop */
    bool requestFakeClipMove(int clipId, int trackId, int position, bool updateView, bool invalidateTimeline, Fun &undo, Fun &redo);
    bool requestFakeClipMove(int clipId, int trackId, int position, bool updateView, bool logUndo, bool invalidateTimeline);
    bool requestFakeGroupMove(int clipId, int groupId, int delta_track, int delta_pos, bool updateView = true, bool logUndo = true);
    bool requestFakeGroupMove(int clipId, int groupId, int delta_track, int delta_pos, bool updateView, bool finalMove, Fun &undo, Fun &redo,
                              bool allowViewRefresh = true);

    /* @brief Given an intended move, try to suggest a more valid one
       (accounting for snaps and missing UI calls)
       @param clipId id of the clip to
       move
       @param trackId id of the target track
       @param position target position
       @param snapDistance the maximum distance for a snap result, -1 for no snapping
        of the clip
       @param dontRefreshMasterClip when false, no view refresh is attempted
        */
    Q_INVOKABLE int suggestItemMove(int itemId, int trackId, int position, int cursorPosition, int snapDistance = -1);
    Q_INVOKABLE int suggestClipMove(int clipId, int trackId, int position, int cursorPosition, int snapDistance = -1);
    Q_INVOKABLE int suggestCompositionMove(int compoId, int trackId, int position, int cursorPosition, int snapDistance = -1);

    /* @brief Request clip insertion at given position. This action is undoable
       Returns true on success. If it fails, nothing is modified.
       @param binClipId id of the clip in the bin
       @param track Id of the track where to insert
       @param position Requested position
       @param ID return parameter of the id of the inserted clip
       @param logUndo if set to false, no undo object is stored
       @param refreshView whether the view should be refreshed
       @param useTargets: if true, the Audio/video split will occur on the set targets. Otherwise, they will be computed as an offset from the middle line
    */
    bool requestClipInsertion(const QString &binClipId, int trackId, int position, int &id, bool logUndo = true, bool refreshView = false,
                              bool useTargets = true);
    /* Same function, but accumulates undo and redo*/
    bool requestClipInsertion(const QString &binClipId, int trackId, int position, int &id, bool logUndo, bool refreshView, bool useTargets, Fun &undo,
                              Fun &redo);

protected:
    /* @brief Creates a new clip instance without inserting it.
       This action is undoable, returns true on success
       @param binClipId: Bin id of the clip to insert
       @param id: return parameter for the id of the newly created clip.
       @param state: The desired clip state (original, audio/video only).
     */
    bool requestClipCreation(const QString &binClipId, int &id, PlaylistState::ClipState state, double speed, Fun &undo, Fun &redo);

public:
    /* @brief Deletes the given clip or composition from the timeline.
       This action is undoable.
       Returns true on success. If it fails, nothing is modified.
       If the clip/composition is in a group, the call is deferred to requestGroupDeletion
       @param clipId is the ID of the clip/composition
       @param logUndo if set to false, no undo object is stored */
    Q_INVOKABLE bool requestItemDeletion(int itemId, bool logUndo = true);
    /* Same function, but accumulates undo and redo*/
    bool requestItemDeletion(int itemId, Fun &undo, Fun &redo);

    /* @brief Move a group to a specific position
       This action is undoable
       Returns true on success. If it fails, nothing is modified.
       If the clips in the group are not in inserted in a track yet, they get inserted for the first time.
       @param clipId is the id of the clip that triggers the group move
       @param groupId is the id of the group
       @param delta_track is the delta applied to the track index
       @param delta_pos is the requested position change
       @param updateView if set to false, no signal is sent to qml for the clip clipId
       @param logUndo if set to true, an undo object is created
       @param allowViewRefresh if false, the view will never get updated (useful for suggestMove)
    */
    bool requestGroupMove(int itemId, int groupId, int delta_track, int delta_pos, bool updateView = true, bool logUndo = true);
    bool requestGroupMove(int itemId, int groupId, int delta_track, int delta_pos, bool updateView, bool finalMove, Fun &undo, Fun &redo,
                          bool allowViewRefresh = true);

    /* @brief Deletes all clips inside the group that contains the given clip.
       This action is undoable
       Note that if their is a hierarchy of groups, all of them will be deleted.
       Returns true on success. If it fails, nothing is modified.
       @param clipId is the id of the clip that triggers the group deletion
    */
    Q_INVOKABLE bool requestGroupDeletion(int clipId, bool logUndo = true);
    bool requestGroupDeletion(int clipId, Fun &undo, Fun &redo);

    /* @brief Change the duration of an item (clip or composition)
       This action is undoable
       Returns the real size reached (can be different, if snapping occurs).
       If it fails, nothing is modified, and -1 is returned
       @param itemId is the ID of the item
       @param size is the new size of the item
       @param right is true if we change the right side of the item, false otherwise
       @param logUndo if set to true, an undo object is created
       @param snap if set to true, the resize order will be coerced to use the snapping grid
    */
    Q_INVOKABLE int requestItemResize(int itemId, int size, bool right, bool logUndo = true, int snapDistance = -1, bool allowSingleResize = false);

    /* Same function, but accumulates undo and redo and doesn't deal with snapping*/
    bool requestItemResize(int itemId, int size, bool right, bool logUndo, Fun &undo, Fun &redo, bool blockUndo = false);

    /* @brief Group together a set of ids
       The ids are either a group ids or clip ids. The involved clip must already be inserted in a track
       This action is undoable
       Returns the group id on success, -1 if it fails and nothing is modified.
       Typically, ids would be ids of clips, but for convenience, some of them can be ids of groups as well.
       @param ids Set of ids to group
    */
    int requestClipsGroup(const std::unordered_set<int> &ids, bool logUndo = true, GroupType type = GroupType::Normal);
    int requestClipsGroup(const std::unordered_set<int> &ids, Fun &undo, Fun &redo, GroupType type = GroupType::Normal);

    /* @brief Destruct the topmost group containing clip
       This action is undoable
       Returns true on success. If it fails, nothing is modified.
       @param id of the clip to degroup (all clips belonging to the same group will be ungrouped as well)
    */
    bool requestClipUngroup(int itemId, bool logUndo = true);
    /* Same function, but accumulates undo and redo*/
    bool requestClipUngroup(int itemId, Fun &undo, Fun &redo);
    // convenience functions for several ids at the same time
    bool requestClipsUngroup(const std::unordered_set<int> &itemIds, bool logUndo = true);

    /* @brief Create a track at given position
       This action is undoable
       Returns true on success. If it fails, nothing is modified.
       @param Requested position (order). If set to -1, the track is inserted last.
       @param id is a return parameter that holds the id of the resulting track (-1 on failure)
    */
    bool requestTrackInsertion(int pos, int &id, const QString &trackName = QString(), bool audioTrack = false);
    /* Same function, but accumulates undo and redo*/
    bool requestTrackInsertion(int pos, int &id, const QString &trackName, bool audioTrack, Fun &undo, Fun &redo, bool updateView = true);

    /* @brief Delete track with given id
       This also deletes all the clips contained in the track.
       This action is undoable
       Returns true on success. If it fails, nothing is modified.
       @param trackId id of the track to delete
    */
    bool requestTrackDeletion(int trackId);
    /* Same function, but accumulates undo and redo*/
    bool requestTrackDeletion(int trackId, Fun &undo, Fun &redo);

    /* @brief Get project duration
       Returns the duration in frames
    */
    int duration() const;
    static int seekDuration; // Duration after project end where seeking is allowed

    /* @brief Get all the elements of the same group as the given clip.
       If there is a group hierarchy, only the topmost group is considered.
       @param clipId id of the clip to test
    */
    std::unordered_set<int> getGroupElements(int clipId);

    /* @brief Removes all the elements on the timeline (tracks and clips)
     */
    bool requestReset(Fun &undo, Fun &redo);
    /* @brief Updates the current the pointer to the current undo_stack
       Must be called for example when the doc change
    */
    void setUndoStack(std::weak_ptr<DocUndoStack> undo_stack);

protected:
    /* @brief Requests the best snapped position for a clip
       @param pos is the clip's requested position
       @param length is the clip's duration
       @param pts snap points to ignore (for example currently moved clip)
       @param snapDistance the maximum distance for a snap result, -1 for no snapping
       @returns best snap position or -1 if no snap point is near
     */
    int getBestSnapPos(int pos, int length, const std::vector<int> &pts = std::vector<int>(), int cursorPosition = 0, int snapDistance = -1);

public:
    /* @brief Requests the next snapped point
       @param pos is the current position
     */
    int getNextSnapPos(int pos);

    /* @brief Requests the previous snapped point
       @param pos is the current position
     */
    int getPreviousSnapPos(int pos);

    /* @brief Add a new snap point
       @param pos is the current position
     */
    void addSnap(int pos);

    /* @brief Remove snap point
       @param pos is the current position
     */
    void removeSnap(int pos);

    /* @brief Request composition insertion at given position.
       This action is undoable
       Returns true on success. If it fails, nothing is modified.
       @param transitionId Identifier of the Mlt transition to insert (as given by repository)
       @param track Id of the track where to insert
       @param position Requested position
       @param length Requested initial length.
       @param id return parameter of the id of the inserted composition
       @param logUndo if set to false, no undo object is stored
    */
    bool requestCompositionInsertion(const QString &transitionId, int trackId, int position, int length, std::unique_ptr<Mlt::Properties> transProps, int &id,
                                     bool logUndo = true);
    /* Same function, but accumulates undo and redo*/
    bool requestCompositionInsertion(const QString &transitionId, int trackId, int compositionTrack, int position, int length,
                                     std::unique_ptr<Mlt::Properties> transProps, int &id, Fun &undo, Fun &redo, bool finalMove = false);

    /* @brief This function change the global (timeline-wise) enabled state of the effects
       It disables/enables track and clip effects (recursively)
     */
    void setTimelineEffectsEnabled(bool enabled);

    /* @brief Get a timeline clip id by its position or -1 if not found
     */
    int getClipByPosition(int trackId, int position) const;

    /* @brief Get a timeline composition id by its starting position or -1 if not found
     */
    int getCompositionByPosition(int trackId, int position) const;

    /* @brief Returns a list of all items that are intersect with a given range.
     * @param trackId is the id of the track for concerned items. Setting trackId to -1 returns items on all tracks
     * @param start is the position where we the items should start
     * @param end is the position after which items will not be selected, set to -1 to get all clips on track
     * @param listCompositions if enabled, the list will also contains composition ids
     */
    std::unordered_set<int> getItemsInRange(int trackId, int start, int end = -1, bool listCompositions = true);

    /* @brief Returns a list of all luma files used in the project
     */
    QStringList extractCompositionLumas() const;
    /* @brief Inform asset view of duration change
     */
    virtual void adjustAssetRange(int clipId, int in, int out);

    void requestClipReload(int clipId);
    void requestClipUpdate(int clipId, const QVector<int> &roles);
    /** @brief define current edit mode (normal, insert, overwrite */
    void setEditMode(TimelineMode::EditMode mode);
    Q_INVOKABLE bool normalEdit() const;

    /** @brief Returns the effectstack of a given clip. */
    std::shared_ptr<EffectStackModel> getClipEffectStack(int itemId);
    std::shared_ptr<EffectStackModel> getTrackEffectStackModel(int trackId);

    /** @brief Add slowmotion effect to clip in timeline.
     @param clipId id of the target clip
    @param speed: speed in percentage. 100 corresponds to original speed, 50 to half the speed
    This functions create an undo object and also apply the effect to the corresponding audio if there is any.
    Returns true on success, false otherwise (and nothing is modified)
    */
    bool requestClipTimeWarp(int clipId, double speed);
    /* @brief Same function as above, but doesn't check for paired audio and accumulate undo/redo
     */
    bool requestClipTimeWarp(int clipId, double speed, Fun &undo, Fun &redo);

    void replugClip(int clipId);

    /** @brief Refresh the tractor profile in case a change was requested. */
    void updateProfile(Mlt::Profile *profile);

    /** @brief Clear the current selection
        @param onDeletion is true when the selection is cleared as a result of a deletion
     */
    Q_INVOKABLE void requestClearSelection(bool onDeletion = false);
    // same function with undo/redo accumulation
    void requestClearSelection(bool onDeletion, Fun &undo, Fun &redo);

    /** @brief Add the given item to the selection
        If @param clear is true, the selection is first cleared
     */
    Q_INVOKABLE void requestAddToSelection(int itemId, bool clear = false);

    /** @brief Remove the given item from the selection */
    Q_INVOKABLE void requestRemoveFromSelection(int itemId);

    /** @brief Set the selection to the set of given ids */
    bool requestSetSelection(const std::unordered_set<int> &ids);
    // same function with undo/redo
    bool requestSetSelection(const std::unordered_set<int> &ids, Fun &undo, Fun &redo);

    /** @brief Returns a set containing all the items in the selection */
    std::unordered_set<int> getCurrentSelection() const;

protected:
    /* @brief Register a new track. This is a call-back meant to be called from TrackModel
       @param pos indicates the number of the track we are adding. If this is -1, then we add at the end.
     */
    void registerTrack(std::shared_ptr<TrackModel> track, int pos = -1, bool doInsert = true, bool reloadView = true);

    /* @brief Register a new clip. This is a call-back meant to be called from ClipModel
     */
    void registerClip(const std::shared_ptr<ClipModel> &clip, bool registerProducer = false);

    /* @brief Register a new composition. This is a call-back meant to be called from CompositionModel
     */
    void registerComposition(const std::shared_ptr<CompositionModel> &composition);

    /* @brief Register a new group. This is a call-back meant to be called from GroupsModel
     */
    void registerGroup(int groupId);

    /* @brief Deregister and destruct the track with given id.
       @parame updateView Whether to send updates to the model. Must be false when called from a constructor/destructor
     */
    Fun deregisterTrack_lambda(int id, bool updateView = false);

    /* @brief Return a lambda that deregisters and destructs the clip with given id.
       Note that the clip must already be deleted from its track and groups.
     */
    Fun deregisterClip_lambda(int id);

    /* @brief Return a lambda that deregisters and destructs the composition with given id.
     */
    Fun deregisterComposition_lambda(int compoId);

    /* @brief Deregister a group with given id
     */
    void deregisterGroup(int id);

    /* @brief Helper function to get a pointer to the track, given its id
     */
    std::shared_ptr<TrackModel> getTrackById(int trackId);
    const std::shared_ptr<TrackModel> getTrackById_const(int trackId) const;

    /*@brief Helper function to get a pointer to a clip, given its id*/
    std::shared_ptr<ClipModel> getClipPtr(int clipId) const;

    /*@brief Helper function to get a pointer to a composition, given its id*/
    std::shared_ptr<CompositionModel> getCompositionPtr(int compoId) const;

    /* @brief Returns next valid unique id to create an object
     */
    static int getNextId();

    /* @brief unplant and the replant all the compositions in the correct order
       @param currentCompo is the id of a compo that have not yet been planted, if any. Otherwise send -1
     */
    bool replantCompositions(int currentCompo, bool updateView);

    /* @brief Unplant the composition with given Id */
    bool unplantComposition(int compoId);

    /* Internal functions to delete a clip or a composition. In general, you should call requestItemDeletion */
    bool requestClipDeletion(int clipId, Fun &undo, Fun &redo);
    bool requestCompositionDeletion(int compositionId, Fun &undo, Fun &redo);

    /** @brief Check tracks duration and update black track accordingly */
    void updateDuration();
    /** @brief Get a track tag (A1, V1, V2,...) through its id */
    const QString getTrackTagById(int trackId) const;

    /** @brief Attempt to make a clip move without ever updating the view */
    bool requestClipMoveAttempt(int clipId, int trackId, int position);

public:
    /* @brief Debugging function that checks consistency with Mlt objects */
    bool checkConsistency();

protected:
    /* @brief Refresh project monitor if cursor was inside range */
    void checkRefresh(int start, int end);

    /* @brief Send signal to require clearing effet/composition view */
    void clearAssetView(int itemId);

    bool m_blockRefresh;

signals:
    /* @brief signal triggered by clearAssetView */
    void requestClearAssetView(int);
    void requestMonitorRefresh();
    /* @brief signal triggered by track operations */
    void invalidateZone(int in, int out);
    /* @brief signal triggered when a track duration changed (insertion/deletion) */
    void durationUpdated();

    /* @brief Signal sent whenever the selection changes */
    void selectionChanged();

protected:
    std::unique_ptr<Mlt::Tractor> m_tractor;

    std::list<std::shared_ptr<TrackModel>> m_allTracks;

    std::unordered_map<int, std::list<std::shared_ptr<TrackModel>>::iterator>
        m_iteratorTable; // this logs the iterator associated which each track id. This allows easy access of a track based on its id.

    std::unordered_map<int, std::shared_ptr<ClipModel>> m_allClips; // the keys are the clip id, and the values are the corresponding pointers

    std::unordered_map<int, std::shared_ptr<CompositionModel>>
        m_allCompositions; // the keys are the composition id, and the values are the corresponding pointers

    static int next_id; // next valid id to assign

    std::unique_ptr<GroupsModel> m_groups;
    std::shared_ptr<SnapModel> m_snaps;

    std::unordered_set<int> m_allGroups; // ids of all the groups

    std::weak_ptr<DocUndoStack> m_undoStack;

    Mlt::Profile *m_profile;

    // The black track producer. Its length / out should always be adjusted to the projects's length
    std::unique_ptr<Mlt::Producer> m_blackClip;

    mutable QReadWriteLock m_lock; // This is a lock that ensures safety in case of concurrent access

    bool m_timelineEffectsEnabled;

    bool m_id; // id of the timeline itself

    // id of the selection. If -1, there is no selection, if positive, then it might either be the id of the selection group, or the id of an individual
    // item, or, finally, the id of a group which is not of type selection. The last case happens when the selection exactly matches an existing group
    // (in that case we cannot further group it because the selection would have only one child, which is prohibited by design)
    int m_currentSelection = -1;

    // The index of the temporary overlay track in tractor, or -1 if not connected
    int m_overlayTrackCount;

    // The preferred audio target for clip insertion or -1 if not defined
    int m_audioTarget;
    // The preferred video target for clip insertion or -1 if not defined
    int m_videoTarget;
    // Timeline editing mode
    TimelineMode::EditMode m_editMode;

    // what follows are some virtual function that corresponds to the QML. They are implemented in TimelineItemModel
protected:
    virtual void _beginRemoveRows(const QModelIndex &, int, int) = 0;
    virtual void _beginInsertRows(const QModelIndex &, int, int) = 0;
    virtual void _endRemoveRows() = 0;
    virtual void _endInsertRows() = 0;
    virtual void notifyChange(const QModelIndex &topleft, const QModelIndex &bottomright, bool start, bool duration, bool updateThumb) = 0;
    virtual void notifyChange(const QModelIndex &topleft, const QModelIndex &bottomright, const QVector<int> &roles) = 0;
    virtual void notifyChange(const QModelIndex &topleft, const QModelIndex &bottomright, int role) = 0;
    virtual QModelIndex makeClipIndexFromID(int) const = 0;
    virtual QModelIndex makeCompositionIndexFromID(int) const = 0;
    virtual QModelIndex makeTrackIndexFromID(int) const = 0;
    virtual void _resetView() = 0;
};
#endif
