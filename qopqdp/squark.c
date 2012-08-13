#include <string.h>
#include <math.h>
#include "qhmc_qopqdp_common.h"

static char *mtname = "qopqdp.squark";

squark_t *
qopqdp_squark_check(lua_State *L, int idx)
{
  luaL_checkudata(L, idx, mtname);
  squark_t *q = lua_touserdata(L, idx);
  return q;
}

void
qopqdp_squark_array_check(lua_State *L, int idx, int n, squark_t *q[n])
{
  luaL_checktype(L, idx, LUA_TTABLE);
  qassert(lua_objlen(L, idx)==n);
  lua_pushvalue(L, idx); // make copy for indexing convenience
  for(int i=0; i<n; i++) {
    lua_pushnumber(L, i+1);
    lua_gettable(L, -2);
    q[i] = qopqdp_squark_check(L, -1);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
}

static void
qopqdp_squark_free(lua_State *L, int idx)
{
  squark_t *q = qopqdp_squark_check(L, idx);
  QDP_destroy_V(q->cv);
}

static int
qopqdp_squark_gc(lua_State *L)
{
  qopqdp_squark_free(L, -1);
  return 0;
}

static int
qopqdp_squark_zero(lua_State *L)
{
  int narg = lua_gettop(L);
  qassert(narg==1 || narg==2);
  squark_t *q = qopqdp_squark_check(L, 1);
  QDP_Subset sub = QDP_all;
  if(narg!=1) {
    sub = qopqdp_check_subset(L, 2);
  }
  QDP_V_eq_zero(q->cv, sub);
  return 0;
}

static int
qopqdp_squark_point(lua_State *L)
{
  int narg = lua_gettop(L);
  qassert(narg==5);
  squark_t *q = qopqdp_squark_check(L, 1);
  int nd; get_table_len(L, 2, &nd);
  int point[nd]; get_int_array(L, 2, nd, point);
  int color = luaL_checkint(L, 3);
  double re = luaL_checknumber(L, 4);
  double im = luaL_checknumber(L, 5);
  int node = QDP_node_number(point);
  if(node==QDP_this_node) {
    int index = QDP_index(point);
    QLA_ColorVector *qcv = QDP_site_ptr_readwrite_V(q->cv, index);
    QLA_c_eq_r_plus_ir(QLA_elem_V(*qcv,color), re, im);
  }
  return 0;
}

static int
qopqdp_squark_random(lua_State *L)
{
  int narg = lua_gettop(L);
  qassert(narg==1 || narg==2);
  squark_t *q = qopqdp_squark_check(L, 1);
  QDP_Subset sub = QDP_all;
  if(narg!=1) {
    sub = qopqdp_check_subset(L, 2);
  }
  QDP_V_eq_gaussian_S(q->cv, qopqdp_srs, sub);
  QLA_Real r = sqrt(0.5); // normalize to sigma^2 = 1/2
  QDP_V_eq_r_times_V(q->cv, &r, q->cv, sub);
  return 0;
}

static int
qopqdp_squark_set(lua_State *L)
{
  int narg = lua_gettop(L);
  qassert(narg==2 || narg==3);
  squark_t *q1 = qopqdp_squark_check(L, 1);
  squark_t *q2 = qopqdp_squark_check(L, 2);
  QDP_Subset sub = QDP_all;
  if(narg!=2) {
    sub = qopqdp_check_subset(L, 3);
  }
  QDP_V_eq_V(q1->cv, q2->cv, sub);
  return 0;
}

static int
qopqdp_squark_norm2(lua_State *L)
{
  int narg = lua_gettop(L);
  qassert(narg==1 || narg==2);
  squark_t *q = qopqdp_squark_check(L, 1);
  QDP_Subset sub = QDP_all;
  if(narg!=1) {
    const char *s = luaL_checkstring(L, 2);
    if(!strcmp(s,"timeslices")) sub = NULL;
    else sub = qopqdp_check_subset(L, 2);
  }
  if(sub) {
    QLA_Real nrm2;
    QDP_r_eq_norm2_V(&nrm2, q->cv, sub);
    lua_pushnumber(L, nrm2);
  } else { // timeslices
    int nt = QDP_coord_size(3);
    QLA_Real nrm2[nt];
    QDP_Subset *ts = qhmcqdp_get_timeslices();
    QDP_r_eq_norm2_V_multi(nrm2, q->cv, ts, nt);
    push_double_array(L, nt, nrm2);
  }
  return 1;
}

static int
qopqdp_squark_redot(lua_State *L)
{
  int narg = lua_gettop(L);
  qassert(narg==2 || narg==3);
  squark_t *q1 = qopqdp_squark_check(L, 1);
  squark_t *q2 = qopqdp_squark_check(L, 2);
  QDP_Subset sub = QDP_all;
  if(narg!=2) {
    const char *s = luaL_checkstring(L, 3);
    if(!strcmp(s,"timeslices")) sub = NULL;
    else sub = qopqdp_check_subset(L, 3);
  }
  if(sub) {
    QLA_Real redot;
    QDP_r_eq_re_V_dot_V(&redot, q1->cv, q2->cv, sub);
    lua_pushnumber(L, redot);
  } else {
    int nt = QDP_coord_size(3);
    QLA_Real redot[nt];
    QDP_Subset *ts = qhmcqdp_get_timeslices();
    QDP_r_eq_re_V_dot_V_multi(redot, q1->cv, q2->cv, ts, nt);
    push_double_array(L, nt, redot);
  }
  return 1;
}

static int
qopqdp_squark_combine(lua_State *L)
{
  int narg = lua_gettop(L);
  qassert(narg==3 || narg==4);
  squark_t *qd = qopqdp_squark_check(L, 1);
  int nqs; get_table_len(L, 2, &nqs);
  squark_t *qs[nqs]; qopqdp_squark_array_check(L, 2, nqs, qs);
  int nc; get_table_len(L, 3, &nc);
  qassert(nqs==nc);
  double c[nc]; get_double_array(L, 3, nc, c);
  QLA_Real qc[nc]; for(int i=0; i<nc; i++) qc[i] = c[i];
  QDP_Subset sub = QDP_all;
  if(narg>32) {
    sub = qopqdp_check_subset(L, 4);
  }
  QDP_V_eq_r_times_V(qd->cv, &qc[0], qs[0]->cv, sub);
  for(int i=1; i<nqs; i++) {
    QDP_V_peq_r_times_V(qd->cv, &qc[i], qs[i]->cv, sub);
  }
  return 0;
}

static int
qopqdp_squark_symshift(lua_State *L)
{
  int narg = lua_gettop(L);
  qassert(narg==3);
  squark_t *qd = qopqdp_squark_check(L, 1);
  squark_t *qs = qopqdp_squark_check(L, 2);
  gauge_t *g = qopqdp_gauge_check(L, 3);
  int mu = luaL_checkint(L, 4) - 1;
  QDP_ColorVector *t = QDP_create_V();
  QDP_ColorVector *t2 = QDP_create_V();
  QDP_V_eq_sV(t, qs->cv, QDP_neighbor[mu], QDP_forward, QDP_all);
  QDP_V_eq_Ma_times_V(t2, g->links[mu], qs->cv, QDP_all);
  QDP_V_eq_M_times_V(qd->cv, g->links[mu], t, QDP_all);
  QDP_V_peq_V(qd->cv, t2, QDP_all);
  QDP_destroy_V(t);
  QDP_destroy_V(t2);
  return 0;
}

static struct luaL_Reg squark_reg[] = {
  { "__gc",    qopqdp_squark_gc },
  { "zero",    qopqdp_squark_zero },
  { "point",   qopqdp_squark_point },
  { "random",  qopqdp_squark_random },
  { "set",     qopqdp_squark_set },
  { "norm2",   qopqdp_squark_norm2 },
  { "Re_dot",  qopqdp_squark_redot },
  { "combine", qopqdp_squark_combine },
  { "symshift",qopqdp_squark_symshift },
  { NULL, NULL}
};

squark_t *
qopqdp_squark_create(lua_State* L)
{
  squark_t *q = lua_newuserdata(L, sizeof(squark_t));
  q->cv = QDP_create_V();
  QDP_V_eq_zero(q->cv, QDP_all);
  if(luaL_newmetatable(L, mtname)) {
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_register(L, NULL, squark_reg);
  }
  lua_setmetatable(L, -2);
  return q;
}
