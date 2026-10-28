// =====================================================================
//  BENCHMARK : ABR maison  contre  std::set, std::map et vector trie
//  Operations mesurees : insertion, recherche, suppression
//  Deux scenarios : donnees ALEATOIRES puis donnees TRIEES
//
//  Compilation (IMPORTANT : -O2) :
//      g++ -std=c++17 -O2 abr_benchmark.cpp -o bench
//
//  Utilisation :
//      bench              -> tailles jusqu'a 1 000 000
//      bench 4000000      -> tailles jusqu'a 4 000 000 (plus lourd)
//
//  Notes :
//   - vector trie : insertion/suppression en O(n) (decalage memoire),
//     limite a VEC_MAX elements.
//   - ABR en donnees triees : il degenere en liste chainee (hauteur n),
//     insertion totale en O(n^2). Limite a ABR_TRIE_MAX elements.
// =====================================================================
#include <iostream>
#include <iomanip>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <numeric>
#include <random>
#include <chrono>
#include <string>
#include <cstdlib>
using namespace std;
using Clock = chrono::steady_clock;

static volatile long long g_sink = 0;      // empeche le compilateur de supprimer les boucles
static const size_t VEC_MAX      = 100000; // taille max pour le vector trie (O(n) par insertion)
static const size_t ABR_TRIE_MAX = 100000; // taille max pour l'ABR sur donnees triees (O(n^2))

static double msDepuis(Clock::time_point t) {
    return chrono::duration<double, milli>(Clock::now() - t).count();
}

// =====================================================================
//  ABR simple (sans equilibrage)
// =====================================================================
class ABR {
    struct Noeud {
        int cle; Noeud *g = nullptr, *d = nullptr;
        explicit Noeud(int c) : cle(c) {}
    };
    Noeud* racine = nullptr;
public:
    ABR() = default;
    ABR(const ABR&) = delete;
    ABR& operator=(const ABR&) = delete;
    ~ABR() {  // destruction iterative (pas de debordement de pile)
        Noeud* n = racine;
        while (n) {
            if (n->g) { Noeud* g = n->g; n->g = g->d; g->d = n; n = g; }
            else      { Noeud* d = n->d; delete n; n = d; }
        }
    }
    void inserer(int c) {
        Noeud** p = &racine;
        while (*p) {
            if (c < (*p)->cle)      p = &(*p)->g;
            else if (c > (*p)->cle) p = &(*p)->d;
            else return;
        }
        *p = new Noeud(c);
    }
    bool contient(int c) const {
        const Noeud* n = racine;
        while (n) {
            if (c == n->cle) return true;
            n = (c < n->cle) ? n->g : n->d;
        }
        return false;
    }
    // Suppression ITERATIVE : sur donnees triees l'arbre est une liste de
    // n noeuds, une version recursive pourrait faire deborder la pile.
    void supprimer(int c) {
        Noeud** p = &racine;
        while (*p) {
            if (c < (*p)->cle)      p = &(*p)->g;
            else if (c > (*p)->cle) p = &(*p)->d;
            else break;
        }
        Noeud* n = *p;
        if (!n) return;
        if (!n->g)      { *p = n->d; delete n; }
        else if (!n->d) { *p = n->g; delete n; }
        else {
            Noeud** q = &n->d;                 // successeur infixe
            while ((*q)->g) q = &(*q)->g;
            Noeud* m = *q;
            n->cle = m->cle;
            *q = m->d;
            delete m;
        }
    }
};

// =====================================================================
//  Enveloppes (meme interface que l'ABR)
// =====================================================================
struct SetRN {
    set<int> s;
    void inserer(int c) { s.insert(c); }
    bool contient(int c) const { return s.find(c) != s.end(); }
    void supprimer(int c) { s.erase(c); }
};

struct MapRN {
    map<int, int> m;
    void inserer(int c) { m.emplace(c, c); }
    bool contient(int c) const { return m.find(c) != m.end(); }
    void supprimer(int c) { m.erase(c); }
};

// vector maintenu trie : recherche en O(log n), insertion/suppression en O(n)
struct VecTrie {
    vector<int> v;
    void inserer(int c) {
        auto it = lower_bound(v.begin(), v.end(), c);
        if (it == v.end() || *it != c) v.insert(it, c);
    }
    bool contient(int c) const { return binary_search(v.begin(), v.end(), c); }
    void supprimer(int c) {
        auto it = lower_bound(v.begin(), v.end(), c);
        if (it != v.end() && *it == c) v.erase(it);
    }
};

// =====================================================================
//  Mesure des 3 operations (ns par operation, meilleure de N repetitions)
// =====================================================================
struct Mesure { double ins = 1e18, rech = 1e18, supp = 1e18; };

struct Jeu {
    vector<int> data;      // cles a inserer (aleatoire OU croissant)
    vector<int> requetes;  // moitie presentes, moitie absentes
    vector<int> aSuppr;    // la moitie des cles
};

// Cles paires 0,2,4,... ; recherches absentes = impairs (tombent au milieu).
// melange = true : insertion en ordre aleatoire ; false : ordre croissant.
Jeu creerJeu(size_t n, size_t Q, mt19937& gen, bool melange) {
    Jeu j;
    j.data.resize(n);
    for (size_t i = 0; i < n; ++i) j.data[i] = 2 * (int)i;
    if (melange) shuffle(j.data.begin(), j.data.end(), gen);

    uniform_int_distribution<size_t> pos(0, n - 1);
    j.requetes.resize(Q);
    for (size_t i = 0; i < Q; ++i)
        j.requetes[i] = (i % 2 == 0) ? j.data[pos(gen)] : 2 * (int)pos(gen) + 1;

    for (size_t i = 0; i < n; i += 2) j.aSuppr.push_back(j.data[i]);
    return j;
}

template <class DS>
Mesure mesurer(const Jeu& j, int reps) {
    Mesure m;
    for (int r = 0; r < reps; ++r) {
        DS ds;

        auto t = Clock::now();
        for (int x : j.data) ds.inserer(x);
        m.ins = min(m.ins, msDepuis(t) * 1e6 / j.data.size());

        long long s = 0;
        t = Clock::now();
        for (int q : j.requetes) s += ds.contient(q);
        m.rech = min(m.rech, msDepuis(t) * 1e6 / j.requetes.size());
        g_sink += s;

        t = Clock::now();
        for (int x : j.aSuppr) ds.supprimer(x);
        m.supp = min(m.supp, msDepuis(t) * 1e6 / j.aSuppr.size());
    }
    return m;
}

// =====================================================================
//  Affichage
// =====================================================================
struct Col {
    string nom;
    Mesure m;
    bool ok;     // false -> structure non mesuree pour cette taille
};

void ligne(const string& op, const vector<Col>& cols, double Mesure::*champ) {
    cout << left << setw(14) << op << right << fixed << setprecision(1);
    double best = 1e18;
    string gagnant;
    for (const auto& c : cols) {
        if (!c.ok) { cout << setw(18) << "-"; continue; }
        double v = c.m.*champ;
        cout << setw(18) << v;
        if (v < best) { best = v; gagnant = c.nom; }
    }
    cout << "   " << left << gagnant << "\n";
}

int repetitionsAlea(size_t n)  { return n <= 100000 ? 5 : (n <= 1000000 ? 3 : 2); }
int repetitionsTrie(size_t n)  { return n <= 10000 ? 3 : 1; }   // l'ABR trie est tres lent

// =====================================================================
//  Un scenario complet (aleatoire ou trie)
// =====================================================================
void scenario(const string& titre, bool melange, const vector<size_t>& tailles,
              mt19937& gen) {
    cout << "\n##########################################################\n"
         << "#  " << titre << "\n"
         << "##########################################################\n";

    for (size_t n : tailles) {
        int reps = melange ? repetitionsAlea(n) : repetitionsTrie(n);
        size_t Q = melange ? 1000000 : 50000;   // moins de requetes en trie (ABR = liste)
        cout << "\n--- n = " << n << "  (" << reps << " repetition(s), meilleure mesure) ---\n";
        cout.flush();
        Jeu j = creerJeu(n, Q, gen, melange);

        vector<Col> cols;
        bool abrOk = melange || n <= ABR_TRIE_MAX;
        if (abrOk) cols.push_back({"ABR", mesurer<ABR>(j, reps), true});
        else       cols.push_back({"ABR", Mesure{}, false});
        cols.push_back({"set", mesurer<SetRN>(j, reps), true});
        cols.push_back({"map", mesurer<MapRN>(j, reps), true});
        if (n <= VEC_MAX) cols.push_back({"vector trie", mesurer<VecTrie>(j, reps), true});
        else              cols.push_back({"vector trie", Mesure{}, false});

        cout << left << setw(14) << "Operation" << right;
        for (const auto& c : cols) cout << setw(18) << (c.nom + " (ns)");
        cout << "   " << left << "Plus rapide\n"
             << string(14 + 18 * cols.size() + 14, '-') << "\n";
        ligne("Insertion",   cols, &Mesure::ins);
        ligne("Recherche",   cols, &Mesure::rech);
        ligne("Suppression", cols, &Mesure::supp);
    }
}

int main(int argc, char** argv) {
    size_t nMax = (argc > 1) ? (size_t)atoll(argv[1]) : 1000000;
    if (nMax < 1000) nMax = 1000;

    cout << "======== BENCHMARK : ABR  contre  set, map, vector trie ========\n";
#ifdef __VERSION__
    cout << "Compilateur : " << __VERSION__ << "\n";
#endif
#ifdef __OPTIMIZE__
    cout << "Optimisations : OUI\n";
#else
    cout << "Optimisations : NON  <-- compilez avec -O2 pour des mesures fiables !\n";
#endif
    cout << "Resultats en nanosecondes par operation (plus petit = plus rapide)\n";
    cout << "Recherche : moitie cles presentes, moitie absentes\n";
    cout << "Suppression : on retire la moitie des cles\n";
    cout << "vector trie : limite a " << VEC_MAX << " elements (insertion O(n))\n";
    cout << "ABR sur donnees triees : limite a " << ABR_TRIE_MAX << " elements (O(n^2))\n";

    mt19937 gen(12345);

    // Echauffement du processeur avant les vraies mesures
    {
        Jeu w = creerJeu(50000, 200000, gen, true);
        mesurer<ABR>(w, 2);
        mesurer<SetRN>(w, 2);
        mesurer<MapRN>(w, 2);
        mesurer<VecTrie>(w, 1);
    }

    vector<size_t> tailles;
    for (size_t n = 1000; n <= nMax; n *= 10) tailles.push_back(n);
    if (tailles.back() != nMax) tailles.push_back(nMax);

    scenario("SCENARIO 1 : DONNEES ALEATOIRES", true,  tailles, gen);
    scenario("SCENARIO 2 : DONNEES TRIEES (ordre croissant) - l'ABR degenere", false, tailles, gen);

    cout << "\n(ignorer : " << g_sink << ")\n";
    return 0;
}