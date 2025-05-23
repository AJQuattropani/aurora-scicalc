#include "differentiate.h"

static f_node *diff_bin(const f_node *in, vector_size_t argidx,
                        f_attribs *attr);

static f_node *diff_un(const f_node *in, vector_size_t argidx, f_attribs *attr);

static f_node *diff_cnst(const f_node *in, vector_size_t argidx,
                         f_attribs *attr);

static f_node *diff_identity(const f_node *in, vector_size_t argidx,
                             f_attribs *attr);

//-------------------------------------------------------------------//

void differentiate_command(Object *restrict obj, token_array *restrict args) {
  if (2 != args->size) {
    fprintf(stderr, "[ERROR] Please provide one function and one index to a "
                    "variable to simplify.\n");
    *obj = null_object();
    return;
  }
  Object *inp = &args->data[0].token->value;
  f_object *inp_func;
  switch (inp->ty) {
  case PFUNC:
    inp_func = inp->pObject.fObj;
    break;
  case FUNC:
    inp_func = &inp->fObject;
    break;
  default:
    fprintf(stderr, "[ERROR] %s is not a function.\n",
            args->data[0].token->key.cstring);
    *obj = null_object();
    return;
  }
  Object *idx = &args->data[1].token->value;
  vector_size_t argidx;
  switch (idx->ty) {
  case VECTOR:
    if (SCALAR == idx->vLiteral.size) {
      argidx = (vector_size_t)idx->vLiteral.data[0];
      if (argidx < inp_func->attr.argcnt) {
        break;
      }
    }
    __attribute__((fallthrough));
  default:
    fprintf(stderr, "[ERROR] %s is not a valid index.\n",
            args->data[1].token->key.cstring);
    *obj = null_object();
    return;
  }

  f_object out;
  int success = differentiate(&out, inp_func, argidx);
  if (success) {
    fprintf(stderr, "[ERROR] Differentiation failed.\n");
    *obj = null_object();
  }

  *obj = (Object){.fObject = out, .ty = FUNC};
  return;
}

/*void differentiation_cleanup(f_object *restrict out) {
  reorder_cleanup_imp(out->root);
  vector_list inp_args = alloc_vdlist(out->attr.argcnt, out->attr.out_size);
  simplify_cleanup_imp(inp_args.data, out->root, 0, &out->attr);
  free_vdlist(&inp_args);
  update_depth(out->root, 0, &out->attr.depth);
}*/

f_node *differentiate_node(const f_node *restrict in, vector_size_t argidx,
                           f_attribs *attr) {
  switch (in->ty) {
  case BINARY:
    return diff_bin(in, argidx, attr);
  case UNARY:
    return diff_un(in, argidx, attr);
  case CONSTANT:
    return diff_cnst(in, argidx, attr);
  case IDENTITY:
    return diff_identity(in, argidx, attr);
  }
  __UNREACHABLE_BRANCH
}

int differentiate(f_object *restrict out, const f_object *restrict in, vector_size_t argidx) {
  if (argidx >= in->attr.argcnt || argidx < 0)
    return 1;
  out->root = differentiate_node(in->root, argidx, &out->attr);
  if (NULL == out->root) {
    out->root = new_fnode();
    *(out->root) =
        (f_node){.name = m_from_cstr("0"),
                 .priority = 0,
                 .depth_index = 0,
                 .ty = CONSTANT,
                 .cf = {.output = alloc_vdliteral(in->attr.out_size)}};
  }
  out->attr.argcnt = in->attr.argcnt;
  out->attr.out_size = in->attr.out_size;
  out->attr.depth = in->attr.depth;

  //differentiation_cleanup(out);
  simplify_imp(out);
  return 0;
}

f_node *diff_bin(const f_node *restrict in, vector_size_t argidx, f_attribs *restrict attr) {
  f_node *g = in->bf.left;
  f_node *gx = differentiate_node(in->bf.left, argidx, attr);
  f_node *h = in->bf.right;
  f_node *hx = differentiate_node(in->bf.right, argidx, attr);

  if (hx == gx) { // should only occur if both are NULL
    return NULL;
  }

  f_node *f_prime;
  if (vb_add == in->bf.op) { // addition rule
    // f = g + h -> f' = g' + h'
    if (hx == NULL)
      return gx;
    if (gx == NULL)
      return hx;

    f_prime = new_fnode();
    *f_prime = (f_node){.name = m_from_cstr("+"),
                        .ty = BINARY,
                        .priority = in->priority,
                        .bf = {.op = vb_add, .left = gx, .right = hx}};
  } else if (vb_sub == in->bf.op) {
    // f = g - h -> f' = g' - h'
    if (NULL == hx)
      return gx;
    f_prime = new_fnode();
    if (NULL == gx) {
      *f_prime = (f_node){.name = m_from_cstr("--"),
                          .ty = UNARY,
                          .priority = in->priority,
                          .uf = {.op = vu_neg, .in = hx}};
    } else {
      *f_prime = (f_node){.name = m_from_cstr("-"),
                          .ty = BINARY,
                          .priority = in->priority,
                          .bf = {.op = vb_sub, .left = gx, .right = hx}};
    }
  } else if (vb_mul == in->bf.op) { // multiplication rule
    // (gh)' = g'h + gh'
    f_node *gxh, *hxg;
    if (NULL != gx) {
      gxh = new_fnode();
      *gxh = (f_node){
          .name = m_from_cstr("*"),
          .ty = BINARY,
          .priority = in->priority,
          .bf = {.op = vb_mul, .left = gx, .right = copy_fnode_recurse(h)}};
      if (NULL == hx)
        return gxh;
    }
    hxg = new_fnode();
    *hxg = (f_node){
        .name = m_from_cstr("*"),
        .ty = BINARY,
        .priority = in->priority,
        .bf = {.op = vb_mul, .left = hx, .right = copy_fnode_recurse(g)}};
    if (NULL == gx)
      return hxg;
    f_prime = new_fnode();
    *f_prime = (f_node){.name = m_from_cstr("+"),
                        .ty = BINARY,
                        .priority = in->priority,
                        .bf = {.op = vb_add, .left = gxh, .right = hxg}};
  } else if (vb_div == in->bf.op) { // division rule
                                    // (g/h)' = g'/h - gh'/h^2 = g'h - gh' / h^2
    f_node *gxh, *hxg;
    if (NULL != gx) {
      gxh = new_fnode();
      *gxh = (f_node){
          .name = m_from_cstr("*"),
          .ty = BINARY,
          .priority = in->priority,
          .bf = {.op = vb_mul, .left = gx, .right = copy_fnode_recurse(h)}};
      if (NULL == hx)
        return gxh;
    }
    hxg = new_fnode();
    f_node *difference = new_fnode();
    *hxg = (f_node){
        .name = m_from_cstr("*"),
        .ty = BINARY,
        .priority = in->priority,
        .bf = {.op = vb_mul, .left = hx, .right = copy_fnode_recurse(g)}};
    if (NULL == gx) {
      *difference = (f_node){.name = m_from_cstr("--"),
                             .ty = UNARY,
                             .priority = in->priority,
                             .uf = {.op = vu_neg, .in = hxg}};
    } else {
      *difference = (f_node){.name = m_from_cstr("-"),
                             .ty = BINARY,
                             .priority = in->priority,
                             .bf = {.op = vb_sub, .left = gxh, .right = hxg}};
    }
    f_node *quotient = new_fnode();
    {
      f_node *two = new_fnode();
      *two = (f_node){.name = m_from_cstr("2"),
                      .ty = CONSTANT,
                      .priority = in->priority,
                      .cf = {.output = make_scalar(-2.0)}};
      *quotient = (f_node){
          .name = m_from_cstr("-"),
          .ty = BINARY,
          .priority = in->priority,
          .bf = {.op = vb_pow, .left = copy_fnode_recurse(h), .right = two}};
    }
    f_prime = new_fnode();
    *f_prime =
        (f_node){.name = m_from_cstr("/"),
                 .ty = BINARY,
                 .priority = in->priority,
                 .bf = {.op = vb_mul, .left = difference, .right = quotient}};
  } else if (vb_pow == in->bf.op) { // power rule
    // g^h = (h)*g^(h-1) g' + ln(g)*g^h h'
    //     =      fg        +     fh
    f_node *fg;
    if (NULL != gx) {
      {
        fg = new_fnode();
        f_node *hgphm1 = new_fnode();
        {
          f_node *gphm1 = new_fnode();
          {
            f_node *hminus1 = new_fnode();
            {
              f_node *one = new_fnode();
              *one = (f_node){.name = m_from_cstr("1"),
                              .ty = CONSTANT,
                              .priority = in->priority + 1,
                              .cf = {.output = make_scalar(1.0)}};
              *hminus1 = (f_node){.name = m_from_cstr("-"),
                                  .ty = BINARY,
                                  .priority = in->priority + 1,
                                  .bf = {.op = vb_sub,
                                         .left = copy_fnode_recurse(h),
                                         .right = one}};
            }
            *gphm1 = (f_node){.name = m_from_cstr("^"),
                              .ty = BINARY,
                              .priority = in->priority,
                              .bf = {.op = vb_pow,
                                     .left = copy_fnode_recurse(g),
                                     .right = hminus1}};
          }
          *hgphm1 = (f_node){.name = m_from_cstr("*"),
                             .ty = BINARY,
                             .priority = in->priority,
                             .bf = {.op = vb_mul,
                                    .left = copy_fnode_recurse(h),
                                    .right = gphm1}};
        }
        *fg = (f_node){.name = m_from_cstr("*"),
                       .ty = BINARY,
                       .priority = in->priority,
                       .bf = {.op = vb_mul, .left = gx, .right = hgphm1}};
      }
      if (NULL == hx) {
        return fg;
      }
    }

    f_node *fh = new_fnode();
    {
      f_node *lngexp = new_fnode();
      {
        f_node *lng = new_fnode();
        *lng = (f_node){.name = m_from_cstr("log"),
                        .ty = UNARY,
                        .priority = in->priority,
                        .uf = {.op = vu_log, .in = copy_fnode_recurse(g)}};
        *lngexp = (f_node){
            .name = m_from_cstr("*"),
            .ty = BINARY,
            .priority = in->priority,
            .bf = {.op = vb_mul, .left = lng, .right = copy_fnode_recurse(in)}};
      }
      *fh = (f_node){.name = m_from_cstr("*"),
                     .ty = BINARY,
                     .priority = in->priority,
                     .bf = {.op = vb_mul, .left = hx, .right = lngexp}};
    }
    if (NULL == gx)
      return fh;
    f_prime = new_fnode();
    *f_prime = (f_node){.name = m_from_cstr("+"),
                        .ty = BINARY,
                        .priority = in->priority,
                        .bf = {.op = vb_add, .left = fg, .right = fh}};
  } else if (vb_log == in->bf.op) {
    // ln(g)/ln(h) = ((lnh)g'/g - (lng)h'/h)/(lnh)^2
    f_node *func = new_fnode();
    {
      f_node *logg = new_fnode();
      *logg = (f_node){.name = m_from_cstr("log"),
                       .ty = UNARY,
                       .priority = in->priority,
                       .uf = {.op = vu_log, .in = copy_fnode_recurse(g)}};
      f_node *logh = new_fnode();
      *logh = (f_node){.name = m_from_cstr("log"),
                       .ty = UNARY,
                       .priority = in->priority,
                       .uf = {.op = vu_log, .in = copy_fnode_recurse(h)}};
      *func = (f_node){.name = m_from_cstr("/"),
                       .ty = BINARY,
                       .priority = in->priority,
                       .bf = {.op = vb_div, .left = logg, .right = logh}};
    }
    f_prime = differentiate_node(func, argidx, attr);
    free_fnode_recurse(func);
  } else {
    fprintf(stderr, "Failed to find matching function pointer in %s\n",
            __func__);
    exit(1);
  }

  // do in place simplification before returning
  return f_prime;
}

f_node *diff_un(const f_node *restrict in, vector_size_t argidx, f_attribs *restrict attr) {
  f_node *dg = differentiate_node(in->uf.in, argidx, attr);

  if (NULL == dg)
    return NULL;

  f_node *fprime;
  if (vu_neg == in->uf.op) {
    fprime = new_fnode();
    *fprime = *in;
    fprime->uf.in = dg;
  } else if (vu_sin == in->uf.op) {
    fprime = new_fnode();
    f_node *fg = new_fnode();
    *fg = (f_node){.name = m_from_cstr("cos"),
                   .ty = UNARY,
                   .priority = in->priority,
                   .uf = {.op = vu_cos, .in = copy_fnode_recurse(in->uf.in)}};
    *fprime = (f_node){.name = m_from_cstr("*"),
                       .ty = BINARY,
                       .priority = in->priority,
                       .bf = {.op = vb_mul, .left = fg, .right = dg}};
  } else if (vu_cos == in->uf.op) {
    fprime = new_fnode();
    f_node *mfg = new_fnode();
    {
      f_node *fg = new_fnode();
      *fg = (f_node){.name = m_from_cstr("sin"),
                     .ty = UNARY,
                     .priority = in->priority,
                     .uf = {.op = vu_sin, .in = copy_fnode_recurse(in->uf.in)}};
      *mfg = (f_node){.name = m_from_cstr("--"),
                      .ty = UNARY,
                      .priority = in->priority,
                      .uf = {.op = vu_neg, .in = fg}};
    }
    *fprime = (f_node){.name = m_from_cstr("*"),
                       .ty = BINARY,
                       .priority = in->priority,
                       .bf = {.op = vb_mul, .left = mfg, .right = dg}};
  } else if (vu_tan == in->uf.op) {
    fprime = new_fnode();
    f_node *sec2g = new_fnode();
    {
      f_node *secg = new_fnode();
      *secg =
          (f_node){.name = m_from_cstr("sec"),
                   .ty = UNARY,
                   .priority = in->priority,
                   .uf = {.op = vu_sec, .in = copy_fnode_recurse(in->uf.in)}};
      f_node *two = new_fnode();
      *two = (f_node){.name = m_from_cstr("2"),
                      .ty = CONSTANT,
                      .priority = in->priority,
                      .cf = {.output = make_scalar(2.0)}};
      *sec2g = (f_node){.name = m_from_cstr("^"),
                        .ty = BINARY,
                        .priority = in->priority,
                        .bf = {.op = vb_pow, .left = secg, .right = two}};
    }
    *fprime = (f_node){.name = m_from_cstr("*"),
                       .ty = BINARY,
                       .priority = in->priority,
                       .bf = {.op = vb_mul, .left = sec2g, .right = dg}};
  } else if (vu_sec == in->uf.op) {
    fprime = new_fnode();
    f_node *secgtang = new_fnode();
    {
      f_node *tang = new_fnode();
      *tang =
          (f_node){.name = m_from_cstr("tan"),
                   .ty = UNARY,
                   .priority = in->priority,
                   .uf = {.op = vu_tan, .in = copy_fnode_recurse(in->uf.in)}};
      *secgtang = (f_node){
          .name = m_from_cstr("*"),
          .ty = BINARY,
          .priority = in->priority,
          .bf = {.op = vb_mul, .left = copy_fnode_recurse(in), .right = tang}};
    }
    *fprime = (f_node){.name = m_from_cstr("*"),
                       .ty = BINARY,
                       .priority = in->priority,
                       .bf = {.op = vb_mul, .left = secgtang, .right = dg}};
  } else if (vu_csc == in->uf.op) {
    fprime = new_fnode();
    f_node *mcscgcotg = new_fnode();
    {
      f_node *cscgcotg = new_fnode();
      {
        f_node *cotg = new_fnode();
        *cotg =
            (f_node){.name = m_from_cstr("cot"),
                     .ty = UNARY,
                     .priority = in->priority,
                     .uf = {.op = vu_cot, .in = copy_fnode_recurse(in->uf.in)}};
        *cscgcotg = (f_node){.name = m_from_cstr("*"),
                             .ty = BINARY,
                             .priority = in->priority,
                             .bf = {.op = vb_mul,
                                    .left = copy_fnode_recurse(in),
                                    .right = cotg}};
      }
      *mcscgcotg = (f_node){.name = m_from_cstr("*"),
                            .ty = UNARY,
                            .priority = in->priority,
                            .uf = {.op = vu_neg, .in = cscgcotg}};
    }
    *fprime = (f_node){.name = m_from_cstr("*"),
                       .ty = BINARY,
                       .priority = in->priority,
                       .bf = {.op = vb_mul, .left = mcscgcotg, .right = dg}};
  } else if (vu_log == in->uf.op) {
    fprime = new_fnode();
    f_node *invg = new_fnode();
    {
      f_node *m1 = new_fnode();
      *m1 = (f_node){.name = m_from_cstr("-1"),
                     .ty = CONSTANT,
                     .priority = in->priority,
                     .cf = {.output = make_scalar(-1.0)}};
      *invg = (f_node){.name = m_from_cstr("^"),
                       .ty = BINARY,
                       .bf = {.op = vb_pow,
                              .left = copy_fnode_recurse(in->uf.in),
                              .right = m1}};
    }
    *fprime = (f_node){.name = m_from_cstr("*"),
                       .ty = BINARY,
                       .priority = in->priority,
                       .bf = {.op = vb_mul, .left = invg, .right = dg}};
  } else {
    fprintf(stderr, "Failed to find matching function pointer in %s\n",
            __func__);
    exit(1);
  }

  return fprime;
}

f_node *diff_cnst([[maybe_unused]] const f_node *restrict in,
                  [[maybe_unused]] vector_size_t arg_num,
                  [[maybe_unused]] f_attribs * restrict attr) {
  return NULL;
}

f_node *diff_identity(const f_node * restrict in, vector_size_t arg_num,
                      [[maybe_unused]] f_attribs * restrict attr) {
  if (arg_num == in->xf.index) {
    f_node *one = new_fnode();
    *one = (f_node){.name = m_from_cstr("1"),
                    .ty = CONSTANT,
                    .priority = in->priority,
                    .cf = {.output = make_scalar(1.0)}};
    return one;
  } else {
    f_node *zero = new_fnode();
    *zero = (f_node){.name = m_from_cstr("0"),
                     .ty = CONSTANT,
                     .priority = in->priority,
                     .cf = {.output = make_scalar(0.0)}};
    return zero;
  }
}
