// Objective-C face of Engine/Driver for the keyboard extension.
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface VKEditResult : NSObject
@property (nonatomic) NSInteger deleteCount;     // characters to delete before the cursor
@property (nonatomic, copy) NSString *text;      // then insert this
@property (nonatomic) BOOL languageChanged;
@end

@interface VKDriver : NSObject
/** 1 = Tiếng Việt, 0 = English. */
@property (nonatomic) NSInteger language;
@property (nonatomic) BOOL autoSwitch;
@property (nonatomic) double pauseAfterManual;
@property (nonatomic) BOOL spelling;
@property (nonatomic) BOOL restoreIfInvalid;
@property (nonatomic) BOOL modernOrthography;
/** 0 Telex, 1 Simple Telex 1, 2 Simple Telex 2 (D56). */
@property (nonatomic) NSInteger telexStyle;

/** One typed character: a letter, digit, punctuation, space or "\n". */
- (VKEditResult *)typeCharacter:(NSString *)character now:(double)now;
- (VKEditResult *)backspaceAt:(double)now;
/** The VI/EN key. */
- (void)manualSwitchAt:(double)now;
/** The cursor moved or the field changed. */
- (void)resetContext;
@end

/// A letter key's frame, for the fat-finger model.
typedef struct { char letter; float x, y, w, h; } VKKeyRect;

/// Which letter a touch at (x, y) meant, given the letters typed so far in this word (lowercase)
/// and the current mode (Engine/TouchModel: closeness × how likely each key is next).
FOUNDATION_EXTERN char VKPickLetter(float x, float y, const char *prefix, int vietnamese, const VKKeyRect *keys, int n);

/// A suggestion / auto-correct candidate.
typedef struct { char letters[32]; unichar text[48]; int textLen; int vietnamese; double score; } VKCandidate;

/// Candidates for the word typed so far (edits of one key, scored by frequency + where keys were touched).
FOUNDATION_EXTERN int VKCandidates(const char *typed, const float *_Nullable xs, const float *_Nullable ys, int vietnamese,
                                   const VKKeyRect *keys, int n, VKCandidate *out, int max);
/// Whether to replace the typed word with out[0] at the space.
FOUNDATION_EXTERN bool VKShouldCorrect(const char *typed, const VKCandidate *c, int count);
/// The most common words starting with prefix, both languages, current mode first.
FOUNDATION_EXTERN int VKCompletions(const char *prefix, int vietnamese, VKCandidate *out, int max);

/// A valid Vietnamese syllable when typed in Telex ("im", "thanhj").
FOUNDATION_EXTERN bool VKIsVietnameseSpelling(const char *letters);
/// Whether the keys make a word in the dictionary of that language ("im" is Vietnamese, "thats" → "thát" is not).
FOUNDATION_EXTERN bool VKIsWord(int vietnamese, const char *letters);
/// Frequency score of the keys as a word (10 = 10 per million, -1000 = not a word).
FOUNDATION_EXTERN int VKWordScore(int vietnamese, const char *letters);

NS_ASSUME_NONNULL_END
