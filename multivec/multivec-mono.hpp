#pragma once

#include <iostream>
#include <cmath>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <thread>
#include <assert.h>
#include <iomanip> // setprecision
#include <boost/serialization/serialization.hpp>
#include <chrono>
#include <random>
#include <iterator>


using namespace std;
using namespace std::chrono;

vector<string> split(const string& sentence);

const float MAX_EXP = 6;
const int UNIGRAM_TABLE_SIZE = 1e8; // size of the frequency table

typedef vector<float> vec;
typedef vector<vec> mat;

inline float sigmoid(float x) {
    assert(x > -MAX_EXP && x < MAX_EXP);
    return 1 / (1 + exp(-x));
}

inline string lower(string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

struct HuffmanNode {
    static const HuffmanNode UNK; // node for out-of-vocabulary words

    string word;

    vector<int> code;
    vector<int> parents;

    HuffmanNode* left;
    HuffmanNode* right;

    int index;
    int count;

    bool is_leaf;
    bool is_unk;
    bool is_sent_id;

    HuffmanNode() : index(-1), is_unk(true), is_sent_id(false) {}

    HuffmanNode(int index, const string& word, bool is_sent_id = false) :
            word(word), index(index), count(1), is_leaf(true), is_unk(false), is_sent_id(is_sent_id)
    {}

    HuffmanNode(int index, HuffmanNode* left, HuffmanNode* right) :
            left(left), right(right), index(index), count(left->count + right->count), is_leaf(false), is_unk(false), is_sent_id(false)
    {}

    bool operator==(const HuffmanNode& node) const {
        return index == node.index;
    }

    bool operator!=(const HuffmanNode& node) const {
        return !(operator==(node));
    }

    static bool comp(const HuffmanNode* v1, const HuffmanNode* v2) {
        return (v1->count) > (v2->count);
    }
};

struct Config {
    float starting_alpha;
    int dimension;
    int min_count;
    int max_iterations;
    int window_size;
    int n_threads;
    float subsampling;
    bool verbose;
    bool hierarchical_softmax;
    bool skip_gram;
    int negative;
    bool sent_vector;
    bool freeze;

    Config() :
        starting_alpha(0.05),
        dimension(100),
        min_count(5),
        max_iterations(5),
        window_size(5),
        n_threads(4),
        subsampling(1e-03),
        verbose(false), // no serialized
        hierarchical_softmax(false),
        skip_gram(false),
        negative(5),
        sent_vector(false),
        freeze(false) // not serialized
        {}

    virtual void print() const {
        std::cout << std::boolalpha; // to print false/true instead of 0/1
        std::cout << "dimension:   " << dimension << std::endl;
        std::cout << "window size: " << window_size << std::endl;
        std::cout << "min count:   " << min_count << std::endl;
        std::cout << "alpha:       " << starting_alpha << std::endl;
        std::cout << "iterations:  " << max_iterations << std::endl;
        std::cout << "threads:     " << n_threads << std::endl;
        std::cout << "subsampling: " << subsampling << std::endl;
        std::cout << "skip-gram:   " << skip_gram << std::endl;
        std::cout << "HS:          " << hierarchical_softmax << std::endl;
        std::cout << "negative:    " << negative << std::endl;
        std::cout << "sent vector: " << sent_vector << std::endl;
        std::cout << "freeze:      " << freeze << std::endl;
    }
};

class MonolingualModel
{
    friend class BilingualModel;
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive& ar, const unsigned int version) {
        ar & config & input_weights & output_weights & output_weights_hs & sent_weights & vocabulary;
    }

private:
    mat input_weights;
    mat output_weights; // output weights for negative sampling
    mat output_weights_hs; // output weights for hierarchical softmax
    mat sent_weights;
    mat online_sent_weights;

    long long training_words; // total number of words in training file (used to compute word frequencies)
    long long training_lines;
    long long words_processed;

    float alpha;
    Config config;
    map<string, HuffmanNode> vocabulary;
    vector<HuffmanNode*> unigram_table;

    static unsigned long long rand() {
        static unsigned long long next_random(time(NULL));
        next_random = next_random * static_cast<unsigned long long>(25214903917) + 11; // possible race conditions, but who cares...
        return next_random >> 16;
    }
    static float randf() {
        return  (MonolingualModel::rand() & 0xFFFF) / 65536.0f;
    }

    void addWordToVocab(const string& word);
    void reduceVocab();
    void createBinaryTree();
    void assignCodes(HuffmanNode* node, vector<int> code, vector<int> parents) const;
    void initUnigramTable();

    HuffmanNode* getRandomHuffmanNode(); // uses the unigram frequency table to sample a random node

    vector<HuffmanNode> getNodes(const string& sentence) const;
    void subsample(vector<HuffmanNode>& node) const;

    void readVocab(const string& training_file);
    void initNet();
    void initSentWeights();

    void trainChunk(const string& training_file, const vector<long long>& chunks, int chunk_id);

    int trainSentence(const string& sent, int sent_id);
    void trainWord(const vector<HuffmanNode>& nodes, int word_pos, int sent_id);
    void trainWordCBOW(const vector<HuffmanNode>& nodes, int word_pos, int sent_id);
    void trainWordSkipGram(const vector<HuffmanNode>& nodes, int word_pos, int sent_id);

    vec hierarchicalUpdate(const HuffmanNode& node, const vec& hidden, float alpha, bool update = true);
    vec negSamplingUpdate(const HuffmanNode& node, const vec& hidden, float alpha, bool update = true);

    vector<long long> chunkify(const string& filename, int n_chunks);

public:
    MonolingualModel() : training_words(0), training_lines(0), words_processed(0) {} // model with default configuration
    MonolingualModel(Config config) : training_words(0), training_lines(0), words_processed(0), config(config) {}

    vec wordVec(int index, int policy) const;
    vec wordVec(const string& word, int policy = 0) const; // word embedding
    vec sentVec(const string& sentence, int policy = 0); // paragraph vector (Le & Mikolov)
    void sentVec(istream& infile, int policy); // compute paragraph vector for all lines in a stream

    void train(const string& training_file); // training from scratch (resets vocabulary and weights)

    void saveVectorsBin(const string &filename, int policy = 0) const; // saves word embeddings in the word2vec binary format
    void saveVectors(const string &filename, int policy = 0) const; // saves word embeddings in the word2vec text format
    void saveSentVectors(const string &filename) const;

    void load(const string& filename); // loads the entire model
    void save(const string& filename) const; // saves the entire model

    void computeAccuracy(istream& infile, int max_vocabulary_size = 0) const;
};
