#ifndef s9_pn_h
#define s9_pn_h

typedef struct oscilloscope oscilloscope_t;
typedef struct s9_pn s9_pn_t;

typedef struct {
	oscilloscope_t *osc;
} s9_pn_config_t;

s9_pn_t *s9_pn_new(const s9_pn_config_t *config);
void s9_pn_free(s9_pn_t **pn);
void s9_pn_update(s9_pn_t *pn);

#endif /* s9_pn_h */
