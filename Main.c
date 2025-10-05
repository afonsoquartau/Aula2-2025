/* main.c
 *
 * Simulador de Escalonamento (C - single file)
 * Implementa: FIFO, SJF (non-preemptive), RR (quantum 0.5s), MLFQ (3 níveis, quantum 0.5s)
 *
 * Uso:
 *   ./simulador <algorithm> <scenario> [repeat]
 * onde:
 *   algorithm = fifo | sjf | rr | mlfq
 *   scenario  = 1 | 2 | 3 | 4
 *   repeat    = (opcional) número de execuções para calcular médias (default 3)
 *
 * Saída: tabela com métricas por processo (Elapsed, CPU, BLOCKED, FirstRun) - médias
 *
 * Nota: simulação lógica (tempo calculado, sem dormir). Todas as chegadas em t=0.
 *
 * Compilar:
 *   gcc main.c -o simulador -lm
 *
 * Exemplo:
 *   ./simulador rr 2 3
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define QUANTUM 0.5   /* 500 ms */
#define EPS 1e-9

/* ----------------------- Tipos ----------------------- */

typedef struct {
    double when_cpu; /* CPU consumed at which IO starts */
    double duration; /* IO duration (blocked time) */
} IOEvent;

typedef struct {
    char name[16];
    double total_cpu_needed;

    /* IO events array */
    IOEvent *io_events;
    int io_count;

    /* runtime state */
    double remaining;
    double cpu_consumed;
    double blocked_time;
    double first_run_time; /* -1 if not yet run */
    double finish_time;    /* -1 if not finished */
    int next_io_index;
} Process;

typedef struct {
    char name[16];
    double Elapsed;
    double CPU;
    double BLOCKED;
    double FirstRun;
} Result;

/* ------------------- Funções utilitárias ------------------- */

static Process * clone_processes(Process *src, int n) {
    Process *dst = (Process*) malloc(sizeof(Process) * n);
    for (int i = 0; i < n; ++i) {
        dst[i] = src[i]; /* shallow copy */
        /* deep copy IO events */
        if (src[i].io_count > 0) {
            dst[i].io_events = (IOEvent*) malloc(sizeof(IOEvent) * src[i].io_count);
            for (int j = 0; j < src[i].io_count; ++j) dst[i].io_events[j] = src[i].io_events[j];
        } else {
            dst[i].io_events = NULL;
        }
        /* init runtime state */
        dst[i].remaining = src[i].total_cpu_needed;
        dst[i].cpu_consumed = 0.0;
        dst[i].blocked_time = 0.0;
        dst[i].first_run_time = -1.0;
        dst[i].finish_time = -1.0;
        dst[i].next_io_index = 0;
    }
    return dst;
}

static void free_processes(Process *p, int n) {
    for (int i = 0; i < n; ++i) {
        if (p[i].io_events) free(p[i].io_events);
    }
    free(p);
}

/* Consume até dt de CPU do processo.
 * Retorna taken (cpu efetivamente consumido) e io_dur (>=0 se IO ocorreu, -1 se nao) */
static void eat_cpu(Process *p, double dt, double *taken, double *io_dur) {
    *io_dur = -1.0;
    if (p->next_io_index < p->io_count) {
        IOEvent ev = p->io_events[p->next_io_index];
        double cpu_until_io = ev.when_cpu - p->cpu_consumed;
        if (cpu_until_io <= EPS) {
            /* IO deveria ocorrer imediatamente */
            p->next_io_index++;
            p->blocked_time += ev.duration;
            *taken = 0.0;
            *io_dur = ev.duration;
            return;
        }
        double take = dt;
        if (take > cpu_until_io) take = cpu_until_io;
        if (take > p->remaining) take = p->remaining;
        p->cpu_consumed += take;
        p->remaining -= take;
        *taken = take;
        if (fabs(p->cpu_consumed - ev.when_cpu) < 1e-6 || p->cpu_consumed > ev.when_cpu - 1e-9) {
            p->next_io_index++;
            p->blocked_time += ev.duration;
            *io_dur = ev.duration;
        }
        return;
    } else {
        double take = dt;
        if (take > p->remaining) take = p->remaining;
        p->cpu_consumed += take;
        p->remaining -= take;
        *taken = take;
        return;
    }
}

/* verifica se processo terminado */
static int is_done(Process *p) {
    return p->remaining <= EPS;
}

/* copia resultados */
static void fill_result(Result *r, Process *p) {
    r->Elapsed = (p->finish_time < 0) ? 0.0 : p->finish_time;
    r->CPU = p->cpu_consumed;
    r->BLOCKED = p->blocked_time;
    r->FirstRun = (p->first_run_time < 0) ? 0.0 : p->first_run_time;
}

/* ------------------- Cenários ------------------- */

/* scenario 1: A 10, B 15, C 20 */
static Process * make_scenario1(int *out_n) {
    int n = 3;
    Process *ps = (Process*) malloc(sizeof(Process) * n);
    strcpy(ps[0].name, "A"); ps[0].total_cpu_needed = 10.0; ps[0].io_events = NULL; ps[0].io_count = 0;
    strcpy(ps[1].name, "B"); ps[1].total_cpu_needed = 15.0; ps[1].io_events = NULL; ps[1].io_count = 0;
    strcpy(ps[2].name, "C"); ps[2].total_cpu_needed = 20.0; ps[2].io_events = NULL; ps[2].io_count = 0;
    *out_n = n;
    return ps;
}

/* scenario 2: A5 B10 C4 D2 E3 F15 */
static Process * make_scenario2(int *out_n) {
    int n = 6;
    Process *ps = (Process*) malloc(sizeof(Process) * n);
    strcpy(ps[0].name, "A"); ps[0].total_cpu_needed = 5.0; ps[0].io_events = NULL; ps[0].io_count = 0;
    strcpy(ps[1].name, "B"); ps[1].total_cpu_needed = 10.0; ps[1].io_events = NULL; ps[1].io_count = 0;
    strcpy(ps[2].name, "C"); ps[2].total_cpu_needed = 4.0; ps[2].io_events = NULL; ps[2].io_count = 0;
    strcpy(ps[3].name, "D"); ps[3].total_cpu_needed = 2.0; ps[3].io_events = NULL; ps[3].io_count = 0;
    strcpy(ps[4].name, "E"); ps[4].total_cpu_needed = 3.0; ps[4].io_events = NULL; ps[4].io_count = 0;
    strcpy(ps[5].name, "F"); ps[5].total_cpu_needed = 15.0; ps[5].io_events = NULL; ps[5].io_count = 0;
    *out_n = n;
    return ps;
}

/* scenario 3: A-5.csv, B-5.csv, C-5.csv equivalent embedded */
/* We'll create example IO sequences meaningful for testing */
static Process * make_scenario3(int *out_n) {
    int n = 3;
    Process *ps = (Process*) malloc(sizeof(Process) * n);

    /* A: total 5, IO events: at 1.0 (0.5), at 3.0 (0.7) */
    strcpy(ps[0].name, "A"); ps[0].total_cpu_needed = 5.0;
    ps[0].io_count = 2;
    ps[0].io_events = (IOEvent*) malloc(sizeof(IOEvent) * 2);
    ps[0].io_events[0].when_cpu = 1.0; ps[0].io_events[0].duration = 0.5;
    ps[0].io_events[1].when_cpu = 3.0; ps[0].io_events[1].duration = 0.7;

    /* B: total 5, IO events: at 2.0 (0.4) */
    strcpy(ps[1].name, "B"); ps[1].total_cpu_needed = 5.0;
    ps[1].io_count = 1;
    ps[1].io_events = (IOEvent*) malloc(sizeof(IOEvent) * 1);
    ps[1].io_events[0].when_cpu = 2.0; ps[1].io_events[0].duration = 0.4;

    /* C: total 5, IO events: at 0.5 (0.2), at 2.5 (1.0) */
    strcpy(ps[2].name, "C"); ps[2].total_cpu_needed = 5.0;
    ps[2].io_count = 2;
    ps[2].io_events = (IOEvent*) malloc(sizeof(IOEvent) * 2);
    ps[2].io_events[0].when_cpu = 0.5; ps[2].io_events[0].duration = 0.2;
    ps[2].io_events[1].when_cpu = 2.5; ps[2].io_events[1].duration = 1.0;

    *out_n = n;
    return ps;
}

/* scenario 4: A-6.csv, B-6.csv, C-6.csv equivalent embedded */
static Process * make_scenario4(int *out_n) {
    int n = 3;
    Process *ps = (Process*) malloc(sizeof(Process) * n);

    /* A: total 6, IO events */
    strcpy(ps[0].name, "A"); ps[0].total_cpu_needed = 6.0;
    ps[0].io_count = 2;
    ps[0].io_events = (IOEvent*) malloc(sizeof(IOEvent) * 2);
    ps[0].io_events[0].when_cpu = 1.2; ps[0].io_events[0].duration = 0.6;
    ps[0].io_events[1].when_cpu = 4.0; ps[0].io_events[1].duration = 0.8;

    /* B: total 6, IO events */
    strcpy(ps[1].name, "B"); ps[1].total_cpu_needed = 6.0;
    ps[1].io_count = 1;
    ps[1].io_events = (IOEvent*) malloc(sizeof(IOEvent) * 1);
    ps[1].io_events[0].when_cpu = 3.5; ps[1].io_events[0].duration = 0.5;

    /* C: total 6, IO events */
    strcpy(ps[2].name, "C"); ps[2].total_cpu_needed = 6.0;
    ps[2].io_count = 3;
    ps[2].io_events = (IOEvent*) malloc(sizeof(IOEvent) * 3);
    ps[2].io_events[0].when_cpu = 0.8; ps[2].io_events[0].duration = 0.3;
    ps[2].io_events[1].when_cpu = 2.0; ps[2].io_events[1].duration = 0.4;
    ps[2].io_events[2].when_cpu = 4.5; ps[2].io_events[2].duration = 0.6;

    *out_n = n;
    return ps;
}

/* Generic factory */
static Process * make_scenario(int scen, int *out_n) {
    if (scen == 1) return make_scenario1(out_n);
    if (scen == 2) return make_scenario2(out_n);
    if (scen == 3) return make_scenario3(out_n);
    if (scen == 4) return make_scenario4(out_n);
    *out_n = 0;
    return NULL;
}

/* ------------------- Algoritmos de escalonamento ------------------- */

/* FIFO: cada processo corre até IO ou terminar (não preemptivo aqui) */
static Result* run_fifo(Process *orig, int n, int *out_count) {
    Process *procs = clone_processes(orig, n);
    Result *res = (Result*) malloc(sizeof(Result) * n);
    double t = 0.0;
    int idx = 0;
    for (int i = 0; i < n; ++i) {
        Process *p = &procs[i];
        if (p->first_run_time < 0) p->first_run_time = t;
        while (!is_done(p)) {
            double taken, io_dur;
            eat_cpu(p, p->remaining, &taken, &io_dur); /* try to finish or reach next IO */
            t += taken;
            if (io_dur >= 0.0) { t += io_dur; }
        }
        p->finish_time = t;
        fill_result(&res[idx++], p);
        strcpy(res[idx-1].name, p->name);
    }
    *out_count = idx;
    free_processes(procs, n);
    return res;
}

/* SJF non-preemptivo: ordenar por total_cpu_needed e executar cada um até terminar/IO */
static int cmp_total_cpu(const void *a, const void *b) {
    const Process *pa = (const Process*) a;
    const Process *pb = (const Process*) b;
    if (pa->total_cpu_needed < pb->total_cpu_needed) return -1;
    if (pa->total_cpu_needed > pb->total_cpu_needed) return 1;
    return 0;
}

static Result* run_sjf(Process *orig, int n, int *out_count) {
    /* clonamos e ordenamos por total_cpu_needed */
    Process *procs = clone_processes(orig, n);
    qsort(procs, n, sizeof(Process), cmp_total_cpu);
    Result *res = (Result*) malloc(sizeof(Result) * n);
    double t = 0.0;
    int idx = 0;
    for (int i = 0; i < n; ++i) {
        Process *p = &procs[i];
        if (p->first_run_time < 0) p->first_run_time = t;
        while (!is_done(p)) {
            double taken, io_dur;
            eat_cpu(p, p->remaining, &taken, &io_dur);
            t += taken;
            if (io_dur >= 0.0) t += io_dur;
        }
        p->finish_time = t;
        fill_result(&res[idx++], p);
        strcpy(res[idx-1].name, p->name);
    }
    *out_count = idx;
    free_processes(procs, n);
    return res;
}

/* RR: round-robin com quantum QUANTUM */
static Result* run_rr(Process *orig, int n, int *out_count) {
    Process *procs = clone_processes(orig, n);
    /* queue por pointers: simples array dinâmica */
    Process **queue = (Process**) malloc(sizeof(Process*) * n * 2);
    int qstart = 0, qend = 0;
    for (int i = 0; i < n; ++i) queue[qend++] = &procs[i];

    Result *res = (Result*) malloc(sizeof(Result) * n);
    int res_idx = 0;
    double t = 0.0;
    while (qstart < qend) {
        Process *p = queue[qstart++];
        if (p->first_run_time < 0) p->first_run_time = t;
        double taken, io_dur;
        eat_cpu(p, QUANTUM, &taken, &io_dur);
        t += taken;
        if (io_dur >= 0.0) {
            t += io_dur;
        }
        if (!is_done(p)) {
            /* re-enqueue */
            queue[qend++] = p;
        } else {
            p->finish_time = t;
            fill_result(&res[res_idx++], p);
            strcpy(res[res_idx-1].name, p->name);
        }
    }
    free(queue);
    *out_count = res_idx;
    free_processes(procs, n);
    return res;
}

/* MLFQ simples: 3 filas (0..2). Quantum = QUANTUM. Se usar todo o quantum, desce de fila. */
static Result* run_mlfq(Process *orig, int n, int *out_count) {
    const int LEVELS = 3;
    Process *procs = clone_processes(orig, n);
    /* filas de pointers; implementamos com arrays dinâmicos por fila */
    Process ***queues = (Process***) malloc(sizeof(Process**) * LEVELS);
    int *qsize = (int*) malloc(sizeof(int) * LEVELS);
    int *qcap  = (int*) malloc(sizeof(int) * LEVELS);
    for (int i = 0; i < LEVELS; ++i) {
        qcap[i] = n + 4;
        queues[i] = (Process**) malloc(sizeof(Process*) * qcap[i]);
        qsize[i] = 0;
    }
    /* inserir todos na fila 0 */
    for (int i = 0; i < n; ++i) queues[0][qsize[0]++] = &procs[i];

    Result *res = (Result*) malloc(sizeof(Result) * n);
    int res_idx = 0;
    double t = 0.0;
    /* enquanto alguma fila tiver elementos */
    int any = 1;
    while (1) {
        any = 0;
        for (int i = 0; i < LEVELS; ++i) if (qsize[i] > 0) { any = 1; break; }
        if (!any) break;
        /* encontra fila mais alta não vazia */
        int qidx = -1;
        for (int i = 0; i < LEVELS; ++i) if (qsize[i] > 0) { qidx = i; break; }
        if (qidx == -1) break;
        /* pop from queue qidx (FIFO within level) */
        Process *p = queues[qidx][0];
        /* shift left */
        for (int j = 1; j < qsize[qidx]; ++j) queues[qidx][j-1] = queues[qidx][j];
        qsize[qidx]--;
        if (p->first_run_time < 0) p->first_run_time = t;
        double taken, io_dur;
        eat_cpu(p, QUANTUM, &taken, &io_dur);
        t += taken;
        if (io_dur >= 0.0) t += io_dur;
        if (is_done(p)) {
            p->finish_time = t;
            fill_result(&res[res_idx++], p);
            strcpy(res[res_idx-1].name, p->name);
        } else {
            /* se usou todo o quantum, descer (a não ser que esteja na última fila) */
            int new_q = qidx;
            if (fabs(taken - QUANTUM) < 1e-9 || taken > QUANTUM - 1e-9) {
                if (qidx < LEVELS - 1) new_q = qidx + 1;
            } else {
                /* não usou todo o quantum (IO ocorreu cedo) -> mantém nível */
                new_q = qidx;
            }
            /* enqueue em new_q */
            if (qsize[new_q] >= qcap[new_q]) {
                qcap[new_q] *= 2;
                queues[new_q] = (Process**) realloc(queues[new_q], sizeof(Process*) * qcap[new_q]);
            }
            queues[new_q][qsize[new_q]++] = p;
        }
    }

    for (int i = 0; i < LEVELS; ++i) free(queues[i]);
    free(queues); free(qsize); free(qcap);
    *out_count = res_idx;
    free_processes(procs, n);
    return res;
}

/* ------------------- Helper para médias e impressão ------------------- */

static Result* accumulate_results(Result **runs, int run_count, int proc_count) {
    /* runs is array of pointers length run_count, each points to array proc_count results.
     * We assume order of processes is same across runs (by name order is not guaranteed),
     * so we'll average by name matching.
     */
    Result *avg = (Result*) malloc(sizeof(Result) * proc_count);
    for (int i = 0; i < proc_count; ++i) {
        /* initialize with first run */
        avg[i] = runs[0][i];
    }
    /* build name->index map from first run */
    /* For simplicity, we'll treat processes in the order of runs[0]. */
    for (int r = 1; r < run_count; ++r) {
        for (int i = 0; i < proc_count; ++i) {
            /* find in runs[r] the same name */
            int found = -1;
            for (int j = 0; j < proc_count; ++j) {
                if (strcmp(runs[r][j].name, avg[i].name) == 0) { found = j; break; }
            }
            if (found >= 0) {
                avg[i].Elapsed += runs[r][found].Elapsed;
                avg[i].CPU += runs[r][found].CPU;
                avg[i].BLOCKED += runs[r][found].BLOCKED;
                avg[i].FirstRun += runs[r][found].FirstRun;
            } else {
                /* mismatch; but to be robust, skip */
            }
        }
    }
    for (int i = 0; i < proc_count; ++i) {
        avg[i].Elapsed /= run_count;
        avg[i].CPU /= run_count;
        avg[i].BLOCKED /= run_count;
        avg[i].FirstRun /= run_count;
    }
    return avg;
}

static void print_results(const char *algorithm, int scenario, Result *avg, int proc_count) {
    printf("\n=== Resultado médio (algoritmo: %s, cenário: %d) ===\n", algorithm, scenario);
    printf("%6s | %8s | %8s | %8s | %8s\n", "Proc", "Elapsed", "CPU", "BLOCKED", "FirstRun");
    printf("--------------------------------------------------------------\n");
    for (int i = 0; i < proc_count; ++i) {
        printf("%6s | %8.3f | %8.3f | %8.3f | %8.3f\n",
               avg[i].name, avg[i].Elapsed, avg[i].CPU, avg[i].BLOCKED, avg[i].FirstRun);
    }
    printf("--------------------------------------------------------------\n");
}

/* ------------------- Main / CLI ------------------- */

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Uso: %s <algorithm> <scenario> [repeat]\n", argv[0]);
        printf(" algorithm = fifo | sjf | rr | mlfq\n");
        printf(" scenario = 1 | 2 | 3 | 4\n");
        printf(" repeat = (opcional) número de execuções para média (default 3)\n");
        return 1;
    }
    const char *alg = argv[1];
    int scenario = atoi(argv[2]);
    int repeat = 3;
    if (argc >= 4) repeat = atoi(argv[3]);
    if (repeat < 1) repeat = 1;

    int base_n;
    Process *base = make_scenario(scenario, &base_n);
    if (!base) {
        fprintf(stderr, "Cenário inválido: %d\n", scenario);
        return 1;
    }

    /* runs will store pointers to result arrays for each run */
    Result **runs = (Result**) malloc(sizeof(Result*) * repeat);
    int proc_count = 0;

    for (int r = 0; r < repeat; ++r) {
        int out_count = 0;
        Result *res = NULL;
        if (strcmp(alg, "fifo") == 0) {
            res = run_fifo(base, base_n, &out_count);
        } else if (strcmp(alg, "sjf") == 0) {
            res = run_sjf(base, base_n, &out_count);
        } else if (strcmp(alg, "rr") == 0) {
            res = run_rr(base, base_n, &out_count);
        } else if (strcmp(alg, "mlfq") == 0) {
            res = run_mlfq(base, base_n, &out_count);
        } else {
            fprintf(stderr, "Algoritmo inválido: %s\n", alg);
            free(base);
            return 1;
        }
        runs[r] = res;
        proc_count = out_count;
    }

    Result *avg = accumulate_results(runs, repeat, proc_count);
    print_results(alg, scenario, avg, proc_count);

    /* cleanup */
    for (int r = 0; r < repeat; ++r) free(runs[r]);
    free(runs);
    free(avg);
    free_processes(base, base_n);

    return 0;
}
