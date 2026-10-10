(module
  (func (export "sum8") (param i32 i32) (result i32)
    local.get 0
    i16x8.splat
    local.get 1
    i16x8.splat
    i16x8.add
    i16x8.extract_lane_s 0))
