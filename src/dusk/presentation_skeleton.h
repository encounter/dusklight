#pragma once

#include <stddef.h>

class J3DModel;
class mDoExt_morf_c;
class mDoExt_McaMorf;
class mDoExt_McaMorfSO;
class mDoExt_McaMorf2;

namespace dusk::presentation_skeleton {

void begin_sim_tick();
void clear();

// Captures standard keyed animation, or a hierarchy-preserving final pose for custom calculators,
// joint callbacks, and models attached to another presented skeleton.
void capture_model(J3DModel* model);

// Captures the common game morph controllers, including their persistent morph history.
void capture_morph(mDoExt_McaMorf* morph);
void capture_morph(mDoExt_McaMorfSO* morph);
void capture_morph(mDoExt_McaMorf2* morph);

// Stateful joint/morph callbacks are excluded unless an actor explicitly opts in.
void set_model_callbacks_safe(J3DModel* model, bool safe);
void set_morph_callbacks_safe(mDoExt_morf_c* morph, bool safe);

// Declares that a model's base transform was derived from a presented parent joint. The captured
// pose keeps any additional local offset while inheriting the parent's presentation transform.
void set_model_attachment(J3DModel* model, J3DModel* parentModel, uint16_t parentJoint);

size_t tracked_model_count();
size_t presented_model_count();

}  // namespace dusk::presentation_skeleton
