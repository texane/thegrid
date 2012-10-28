#include <stdlib.h>
#include <stdio.h>
#include <string.h>


/* grid */

enum block_color
{
  BLOCK_COLOR_R = 0,
  BLOCK_COLOR_G,
  BLOCK_COLOR_B,
  BLOCK_COLOR_INVALID
};

static inline char color_to_char(enum block_color x)
{
  switch (x)
  {
  case BLOCK_COLOR_R: return 'r';
  case BLOCK_COLOR_G: return 'g';
  case BLOCK_COLOR_B: return 'b';
  default: break ;
  }
  return '_';
}

typedef struct grid
{
  unsigned int n;
  enum block_color* cells;
} grid_t;

static void grid_init(grid_t* g, unsigned int n)
{
  const unsigned int nn = n * n;
  unsigned int i;
  g->cells = malloc(nn * sizeof(g->cells[0]));
  for (i = 0; i < nn; ++i) g->cells[i] = BLOCK_COLOR_INVALID;
  g->n = n;
}

static void grid_init_with_grid(grid_t* g, const grid_t* fu)
{
  const unsigned int nn = fu->n * fu->n;
  unsigned int i;
  g->cells = malloc(nn * sizeof(g->cells[0]));
  for (i = 0; i < nn; ++i) g->cells[i] = fu->cells[i];
  g->n = fu->n;
}

static void grid_fini(grid_t* g)
{
  free(g->cells);
}

static inline enum block_color* grid_at
(grid_t* g, unsigned int x, unsigned int y)
{
  return g->cells + y * g->n + x;
}

static inline enum block_color* grid_const_at
(const grid_t* g, unsigned int x, unsigned int y)
{
  return grid_at((grid_t*)g, x, y);
}

static void grid_print(const grid_t* g)
{
  const unsigned int nn = g->n * g->n;
  unsigned int i;

  for (i = 0; i < nn; ++i)
  {
    if (i && ((i % g->n) == 0)) printf("\n");
    printf(" %c", color_to_char(g->cells[i]));
  }
  printf("\n");
}


/* rules */

typedef struct rule
{
  struct rule* next;
  unsigned int outcome;
  int (*try_match)(const struct rule*, const grid_t*);

  union
  {
    struct
    {
      unsigned int n;
    } ncons;
  } u;

} rule_t;

typedef struct rulset
{
  struct rule* rules;
} rulset_t;


/* ncons rule */

static int try_match_ncons_rule(const rule_t* r, const grid_t* g)
{
  unsigned int i;
  unsigned int j;
  unsigned int k;

  for (i = 0; i < g->n; ++i)
  {
    for (j = 0; j < g->n; ++j)
    {
      /* horizontal */
      if ((g->n - i) >= r->u.ncons.n)
      {
	for (k = 0; *grid_const_at(g, j + k, i) != BLOCK_COLOR_INVALID; ++k)
	  if (k == (r->u.ncons.n - 1)) return 0;
      }

      /* vertical */
      if ((g->n - j) >= r->u.ncons.n)
      {
	for (k = 0; *grid_const_at(g, i, j + k) != BLOCK_COLOR_INVALID; ++k)
	  if (k == (r->u.ncons.n - 1)) return 0;
      }
    }
  }

  return -1;
}

static rule_t* make_ncons_rule(void)
{
  static const unsigned int ncons = 4;

  rule_t* const rule = malloc(sizeof(rule_t));

  rule->next = NULL;
  rule->outcome = ncons;
  rule->try_match = try_match_ncons_rule;
  rule->u.ncons.n = ncons;

  return rule;
}

static void rulset_init(rulset_t* rulset)
{
  rule_t* rule;

  rulset->rules = NULL;

  /* generate some rules */

  rule = make_ncons_rule();
  rule->next = rulset->rules;
  rulset->rules = rule;
}

static void rulset_fini(rulset_t* r)
{
  rule_t* ru = r->rules;
  while (ru)
  {
    rule_t* const tmp = ru;
    ru = ru->next;
    free(tmp);
  }
}


/* state */

typedef struct state
{
  unsigned int items[3];

  enum block_color cur_color;
  rule_t* cur_rule;

  /* distance from the current rule */
  unsigned int cur_dist;

  unsigned int is_done;

  /* TODO: time ... */
} state_t;

static void state_init(state_t* s)
{
  s->items[0] = 100;
  s->items[1] = 100;
  s->items[2] = 100;
  s->cur_color = BLOCK_COLOR_R;
  s->cur_rule = NULL;
  s->cur_dist = (unsigned int)-1;
  s->is_done = 0;
}


/* rule evaluator, breadth first search based */

typedef struct bfs_node
{
  /* parent conf */
  struct bfs_node* parent;

  /* next conf to be explored */
  struct bfs_node* next;

  /* which operation, 0 for put block */
  unsigned int o;

  /* target color */
  enum block_color c;

  /* operation position */
  unsigned int x;
  unsigned int y;

} bfs_node_t;

typedef struct bfs
{
  /* node to be explored */
  bfs_node_t* head;
  bfs_node_t* tail;

  /* explored node */
  bfs_node_t* trash;

  /* resulting distance */
  unsigned int dist;
} bfs_t;


static bfs_node_t* bfs_make_node
(
 unsigned int o,
 enum block_color c,
 unsigned int x, unsigned int y,
 bfs_node_t* p
)
{
  bfs_node_t* const n = malloc(sizeof(bfs_node_t));
  if (n == NULL) return NULL;
  n->next = NULL;
  n->parent = p;
  n->o = o;
  n->x = x;
  n->y = y;
  n->c = c;
  return n;
}

static void bfs_free_node(bfs_node_t* n)
{
  free(n);
}

static void bfs_push_node(bfs_t* b, bfs_node_t* n)
{
  /* push at tail */
  if (b->tail) b->tail->next = n;
  else b->head = n;
  b->tail = n;
}

static bfs_node_t* bfs_pop_node(bfs_t* b)
{
  /* pop from head to trash */
  bfs_node_t* const n = b->head;
  if (n == NULL) return NULL;

  if (n == b->tail) b->tail = NULL;
  b->head = n->next;

  n->next = b->trash;
  b->trash = n;

  return n;
}

static void bfs_do_one(bfs_t* b, bfs_node_t* p, const grid_t* g)
{
  bfs_node_t* n;
  unsigned int x;
  unsigned int y;
  unsigned int i;

  for (x = 0; x < g->n; ++x)
  {
    for (y = 0; y < g->n; ++y)
    {
      const enum block_color c = *grid_const_at(g, x, y);

      if (c != BLOCK_COLOR_INVALID)
      {
	/* get block */
	n = bfs_make_node(1, c, x, y, p);
	bfs_push_node(b, n);
      }
      else /* cell is empty */
      {
	/* put block */
	for (i = 0; i < BLOCK_COLOR_INVALID; ++i)
	{
	  n = bfs_make_node(0, (enum block_color)i, x, y, p);
	  bfs_push_node(b, n);
	}
      }
    }
  }
}

static void redo_grid(bfs_node_t* n, grid_t* g)
{
  int i; /* must be signed */
  bfs_node_t* stack[1024];

  /* goto first node */
  for (i = 0; n->parent; n = n->parent, ++i) stack[i] = n;
  stack[i] = n;

  for (; i >= 0; --i)
  {
    bfs_node_t* const nn = stack[i];
    enum block_color c;
    /* put block, else get */
    if (nn->o == 0) c = nn->c;
    else c = BLOCK_COLOR_INVALID;
    *grid_at(g, nn->x, nn->y) = c;
  }
}

static void undo_grid(bfs_node_t* n, grid_t* g)
{
  for (; n; n = n->parent)
  {
    enum block_color c;

    /* put, thus get back. otherwise, put back. */
    if (n->o == 0) c = BLOCK_COLOR_INVALID;
    else c = n->c;
    *grid_at(g, n->x, n->y) = c;
  }
}

static void bfs_do
(bfs_t* b, const grid_t* ini_grid, const rule_t* r)
{
  grid_t g;
  bfs_node_t* n;

  b->head = NULL;
  b->tail = NULL;
  b->trash = NULL;
  b->dist = (unsigned int)-1;

  grid_init_with_grid(&g, ini_grid);

  bfs_do_one(b, NULL, &g);

  while ((n = bfs_pop_node(b)))
  {
    redo_grid(n, &g);

    if (r->try_match(r, &g) == 0)
    {
#if 1
      printf("found\n");
      grid_print(&g);
      printf("\n");
      fflush(stdout);
#endif

      for (b->dist = 1; n->parent; n = n->parent, ++b->dist) ;
      break ;
    }

    bfs_do_one(b, n, &g);

    undo_grid(n, &g);
  }

  grid_fini(&g);

  for (n = b->trash; n; )
  {
    bfs_node_t* const tmp = n;
    n = n->next;
    bfs_free_node(tmp);
  }
}


/* cmdline */

enum cmdline_op
{
  CMDLINE_OP_PUT_BLOCK = 0,
  CMDLINE_OP_GET_BLOCK,
  CMDLINE_OP_SEL_COLOR,
  CMDLINE_OP_SEL_RULE,
  CMDLINE_OP_LIST_RULES,
  CMDLINE_OP_LIST_ITEMS,
  CMDLINE_OP_EVAL_RULE,
  CMDLINE_OP_PRINT_GRID,
  CMDLINE_OP_QUIT,
  CMDLINE_OP_INVALID
};

typedef struct cmdline
{
  enum cmdline_op op;

  unsigned int x;
  unsigned int y;
  enum block_color color;

  unsigned int rule;

} cmdline_t;

static void cmdline_get(cmdline_t* cmd)
{
  char* line = NULL;
  ssize_t n;
  char fu[4];

  printf("$> "); fflush(stdout);

  cmd->op = CMDLINE_OP_INVALID;

  n = 0;
  n = getline(&line, (size_t*)&n, stdin);
  if (n == 0)
  {
    if (line != NULL) free(line);
    return ;
  }

  /* pb x y: put a block at x,y */
  /* gb x y: get a block at x,y */
  /* sc c: select color c */
  /* sr r: select rule r */
  /* lr: list rules */
  /* li: list items */
  /* er r: eval rule r */
  /* pg: print grid */
  /* q: quit */

#define STRNCMP(__a, __b) memcmp(__a, __b, (sizeof(__b) - 1))
  if (STRNCMP(line, "pb") == 0)
  {
    cmd->op = CMDLINE_OP_PUT_BLOCK;
    sscanf(line, "%s %u %u\n", fu, &cmd->x, &cmd->y);
  }
  else if (STRNCMP(line, "gb") == 0)
  {
    cmd->op = CMDLINE_OP_GET_BLOCK;
    sscanf(line, "%s %u %u\n", fu, &cmd->x, &cmd->y);
  }
  else if (STRNCMP(line, "sc") == 0)
  {
    char rgb;
    cmd->op = CMDLINE_OP_SEL_COLOR;
    sscanf(line, "%s %c\n", fu, &rgb);
    if (rgb == 'r') cmd->color = BLOCK_COLOR_R;
    else if (rgb == 'g') cmd->color = BLOCK_COLOR_G;
    else /* if (rgb == b) */ cmd->color = BLOCK_COLOR_B;
  }
  else if (STRNCMP(line, "sr") == 0)
  {
    cmd->op = CMDLINE_OP_SEL_RULE;
    sscanf(line, "%s %u\n", fu, &cmd->rule);
  }
  else if (STRNCMP(line, "lr") == 0)
  {
    sscanf(line, "%s\n", fu);
    cmd->op = CMDLINE_OP_LIST_RULES;
  }
  else if (STRNCMP(line, "li") == 0)
  {
    sscanf(line, "%s\n", fu);
    cmd->op = CMDLINE_OP_LIST_ITEMS;
  }
  else if (STRNCMP(line, "er") == 0)
  {
    cmd->op = CMDLINE_OP_EVAL_RULE;
    sscanf(line, "%s %u\n", fu, &cmd->rule);
  }
  else if (STRNCMP(line, "pg") == 0)
  {
    sscanf(line, "%s\n", fu);
    cmd->op = CMDLINE_OP_PRINT_GRID;
  }
  else if (STRNCMP(line, "q") == 0)
  {
    sscanf(line, "%s\n", fu);
    cmd->op = CMDLINE_OP_QUIT;
  }

  free(line);
}


/* main */

int main(int ac, char** av)
{
  grid_t g;
  state_t state;
  bfs_t b;
  cmdline_t cmd;
  rulset_t rulset;
  const rule_t* ru;
  unsigned int i;

  grid_init(&g, 5);
  state_init(&state);
  rulset_init(&rulset);

  /* start with first rule */
  state.cur_rule = rulset.rules;

  while (state.is_done == 0)
  {
    cmdline_get(&cmd);

    switch (cmd.op)
    {
    case CMDLINE_OP_PUT_BLOCK:
      if (state.items[state.cur_color])
      {
	if (*grid_at(&g, cmd.x, cmd.y) == BLOCK_COLOR_INVALID)
        {
	  *grid_at(&g, cmd.x, cmd.y) = state.cur_color;
	  --state.items[state.cur_color];
	}
      }
      break ;

    case CMDLINE_OP_GET_BLOCK:
      if (*grid_at(&g, cmd.x, cmd.y) != BLOCK_COLOR_INVALID)
      {
	++state.items[*grid_at(&g, cmd.x, cmd.y)];
	*grid_at(&g, cmd.x, cmd.y) = BLOCK_COLOR_INVALID;
      }
      break ;

    case CMDLINE_OP_SEL_COLOR:
      state.cur_color = cmd.color;
      break ;

    case CMDLINE_OP_SEL_RULE:
      ru = rulset.rules;
      for (i = 0; ru && (i < cmd.rule); ++i, ru = ru->next) ;
      state.cur_rule = (rule_t*)ru;
      if (ru == NULL) { printf("no such rule\n"); break ; }
      goto eval_rule_case;
      break ;

    case CMDLINE_OP_LIST_RULES:
      ru = rulset.rules;
      for (i = 0; ru; ++i, ru = ru->next)
	printf("[%u] %u\n", i, ru->outcome);
      break ;

    case CMDLINE_OP_LIST_ITEMS:
      for (i = 0; i < 3; ++i) printf(" %u", state.items[i]);
      printf("\n");
      break ;

    case CMDLINE_OP_EVAL_RULE:
    eval_rule_case:
      if (state.cur_rule == NULL)
      {
	printf("no rule selected\n");
	break ;
      }

      bfs_do(&b, &g, state.cur_rule);
      state.cur_dist = b.dist;
      printf("distance: %u\n", state.cur_dist);

      break ;

    case CMDLINE_OP_PRINT_GRID:
      grid_print(&g);
      break ;

    case CMDLINE_OP_QUIT:
      state.is_done = 1;
      break ;

    case CMDLINE_OP_INVALID:
    default:
      break ;
    }
  }

  grid_fini(&g);
  rulset_fini(&rulset);

  return 0;
}
