//   ____  _            _
//  | __ )| | ___   ___| | __
//  |  _ \| |/ _ \ / __| |/ /
//  | |_) | | (_) | (__|   <
//  |____/|_|\___/ \___|_|\_\
//   _                _               _                    _
//  | |    ___   ___ | | __      __ _| |__   ___  __ _  __| |
//  | |   / _ \ / _ \| |/ /____ / _` | '_ \ / _ \/ _` |/ _` |
//  | |__| (_) | (_) |   <_____| (_| | | | |  __/ (_| | (_| |
//  |_____\___/ \___/|_|\_\     \__,_|_| |_|\___|\__,_|\__,_|
//
// Implement here block-related functions for look-ahead

#include "block_la.h"

//  _   _                 _____          _
// | \ | | _____      __ |  ___|__  __ _| |_ _   _ _ __ ___  ___
// |  \| |/ _ \ \ /\ / / | |_ / _ \/ _` | __| | | | '__/ _ \/ __|
// | |\  |  __/\ V  V /  |  _|  __/ (_| | |_| |_| | | |  __/\__ \
// |_| \_|\___| \_/\_/   |_|  \___|\__,_|\__|\__,_|_|  \___||___/
// ============================================================================

//  ____ _____ _____ ____    _
// / ___|_   _| ____|  _ \  / |
// \___ \ | | |  _| | |_) | | |
//  ___) || | | |___|  __/  | |
// |____/ |_| |_____|_|     |_|
// Intermediate velocities

data_t dot_product(const point_t *p1, const point_t *p2, const point_t *p3) {
  assert(p1 && p2 && p3);
  return fabs((point_x(p2) - point_x(p1)) * (point_x(p3) - point_x(p2)) +
              (point_y(p2) - point_y(p1)) * (point_y(p3) - point_y(p2)) +
              (point_z(p2) - point_z(p1)) * (point_z(p3) - point_z(p2)));
}

// ORDERING ELEMENTS IN ASCENDENT ORDER
void ascending(data_t *vector, data_t size) {
  assert(vector);
  int i = 0;
  while (i < size) {
    // SWAPPING
    if (vector[i] > vector[i + 1]) {
      data_t tmp = vector[i];
      vector[i] = vector[i + 1];
      vector[i + 1] = tmp;
    }
    i++;
  }
}

data_t cosAlpha(const block_t *b) {
  assert(b);
  data_t cos_alpha;
  point_t *p1 = point_zero(b);
  point_t *p2 = block_target(b);
  point_t *p3 = block_target(block_next(b));
  point_t *center = block_center(b);
  point_t *center_next = block_center(block_next(b));

  data_t v1_lin = point_dist(p1, p2);
  data_t v2_lin = point_dist(p2, p3);
  data_t v1_arc = point_dist(center, p2);
  data_t v2_arc = point_dist(p2, center_next);

  // LINEAR BLOCK -- LINEAR BLOCK
  if (block_type(b) == LINE && block_type(block_next(b)) == LINE)
    cos_alpha = dot_product(p1, p2, p3) / (v1_lin * v2_lin);

  // ARC_CW or ARC_CCW BLOCK -- LINEAR BLOCK
  else if ((block_type(b) == ARC_CW || block_type(b) == ARC_CCW) &&
           block_type(block_next(b)) == LINE) {
    data_t dot = dot_product(center, p1, p2);
    data_t beta = acos(dot / (v1_arc * v2_lin)); // radiants
    cos_alpha = cos(M_PI / 2.0 - beta);
  }

  // LINEAR BLOCK -- ARC_CW or ARC_CCW BLOCK
  else if (block_type(b) == LINE && (block_type(block_next(b)) == ARC_CW ||
                                     block_type(block_next(b)) == ARC_CCW)) {
    data_t dot = dot_product(p1, p2, center_next);
    data_t beta = acos(dot / (v1_lin * v2_arc)); // radiants
    cos_alpha = cos(M_PI / 2.0 - beta);
  }

  // all 4 combinations of ARC_CW and ARC_CCW blocks
  else {
    data_t dot = dot_product(center, p2, center_next);
    cos_alpha = dot / (v1_arc * v2_arc);
  }
  return cos_alpha;
}

data_t maintenanceVel(const block_t *b) {
  assert(b);
  data_t vm;

  // LINEAR block
  if (block_type(b) == LINE)
    vm = b->feedrate; // [mm/min]

  // CLOCK or COUNTERCLOCKWISE block
  else if (block_type(b) == ARC_CW || block_type(b) == ARC_CCW)
    vm = sqrt(machine_A(b->machine) * block_r(b)) * 60; // [mm/min]

  return vm;
}

data_t finalVel(const block_t *b) {
  assert(b);

  data_t vm = maintenanceVel(b);
  data_t vm_next = maintenanceVel(block_next(b));
  data_t cos_alpha = cosAlpha(b);

  if (cos_alpha > (sqrt(2) / 2)) // angle > 45?? ==> 0 speed
    return 0.0;

  return (vm + vm_next) / 2.0 * cos_alpha;
}

data_t initialVel(const block_t *b) {
  assert(b);
  data_t vi;
  if (b->prev)
    vi = finalVel(b->prev);

  return vi;
}

void settingVel_sisf(const block_t *b) {
  assert(b);
  b->prof->initialVel = initialVel(b);
  b->prof->manintenanceVel = maintenanceVel(b);
  b->prof->finalVel = finalVel(b);
  
  // si
  b->prof->s[0] = 0.0;
  // sf
  b->prof->s[S_LENGTH - 1] = block_length(b);
}

//  ____ _____ _____ ____    ____      _____
// / ___|_   _| ____|  _ \  |___ \    |___ /
// \___ \ | | |  _| | |_) |   __) |____ |_ \
//  ___) || | | |___|  __/   / __/_____|__) |
// |____/ |_| |_____|_|     |_____|   |____/

// ============================= Accelerations =================================
void s1s2_Acc(const block_t *b, data_t MAX_acc, data_t vi, data_t vm, data_t vf,
              data_t si, data_t sf) {
  if (vi <= vm)
    b->prof->s[1] = si + (pow(vm, 2) - pow(vi, 2)) / (2.0 * MAX_acc);

  if (vm <= vf)
    b->prof->s[2] = sf + (pow(vm, 2) - pow(vf, 2)) / (2.0 * MAX_acc);
  else
    s1s2_Dec(b, MAX_acc, vi, vm, vf, si, sf);
}

// ============================= Decelerations =================================
void s1s2_Dec(const block_t *b, data_t MAX_acc, data_t vi, data_t vm, data_t vf,
              data_t si, data_t sf) {
  if (vi > vm)
    b->prof->s[1] = si + (pow(vi, 2) - pow(vm, 2)) / (2.0 * MAX_acc);

  if (vm > vf)
    b->prof->s[2] = sf + (pow(vf, 2) - pow(vm, 2)) / (2.0 * MAX_acc);
  else
    s1s2_Acc(b, MAX_acc, vi, vm, vf, si, sf);
}

//  ____ _____ _____ ____    _  _
// / ___|_   _| ____|  _ \  | || |
// \___ \ | | |  _| | |_) | | || |_
//  ___) || | | |___|  __/  |__   _|
// |____/ |_| |_____|_|        |_|
// Calculate the timings
void timings(const block_t *b, data_t MAX_acc, data_t vi, data_t vm, data_t vf,
              data_t si, data_t s1, data_t s2, data_t sf) 
{ 
  assert(b);
  // when we accelerate
  b->prof->t1 = (1 / MAX_acc) * (-vi + sqrt(2 * MAX_acc * (s1 - si) + pow(vi, 2)));
  b->prof->t2 = (1 / MAX_acc) * (-vm + sqrt(2 * MAX_acc * (s2 - s1) + pow(vm, 2)));
  b->prof->tf = (1 / MAX_acc) * (-vm + sqrt(2 * MAX_acc * (sf - s2) + pow(vm, 2)));
  // :......
}
//  ____ _____ _____ ____    ____
// / ___|_   _| ____|  _ \  | ___|
// \___ \ | | |  _| | |_) | |___ \
//  ___) || | | |___|  __/   ___) |
// |____/ |_| |_____|_|     |____/
// Reshaping
void reshaping(const block_t *b, data_t tf, data_t tq)
{
  assert(b);
  data_t tf_star = ((size_t)(tf / tq) + 1) * tq;
  data_t k = tf_star / tf;

  // rescaling velocities
  b->prof->initialVel /= k;
  b->prof->maintenanceVel /= k;
  b->prof->finalVel /= k;
}

//   _____ _____ ____ _____   __  __       _
//  |_   _| ____/ ___|_   _| |  \/  | __ _(_)_ __
//    | | |  _| \___ \ | |   | |\/| |/ _` | | '_ \
//    | | | |___ ___) || |   | |  | | (_| | | | | |
//    |_| |_____|____/ |_|   |_|  |_|\__,_|_|_| |_|
//
#ifdef BLOCK_LA_MAIN
int main() {
  block_t *b1 = NULL, *b2 = NULL, *b3 = NULL;
  machine_t *cfg = machine_new(NULL);

  b1 = block_new("N10 G00 X90 Y90 Z100 t3", NULL, cfg);
  block_parse(b1);
  b2 = block_new("N20 G01 Y100 X100 F1000 S2000", b1, cfg);
  block_parse(b2);
  b3 = block_new("N30 G01 Y200", b2, cfg);
  block_parse(b3);

  // TEST FOR LOOK AHEAD
  data_t res = cosAlpha(b1);

  printf("cosAlpha = %f\n", res);

  block_print(b1, stdout);
  block_print(b2, stdout);
  block_print(b3, stdout);

  block_free(b1);
  block_free(b2);
  block_free(b3);
  return 0;
}
#endif