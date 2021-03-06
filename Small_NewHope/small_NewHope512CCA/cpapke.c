#include <stdio.h>
#include "api.h"
#include "poly.h"
#include "rng.h"
#include "fips202.h"
#include "pt_ntt.h"

/*************************************************
* Name:        encode_pk
* 
* Description: Serialize the public key as concatenation of the
*              serialization of the polynomial pk and the public seed
*              used to generete the polynomial a.
*
* Arguments:   unsigned char *r:          pointer to the output serialized public key
*              const poly *pk:            pointer to the input public-key polynomial
*              const unsigned char *seed: pointer to the input public seed
**************************************************/
static void encode_pk(unsigned char *r, const poly *pk, const unsigned char *seed)
{
  int i;
  poly_tobytes(r, pk);
  for(i=0;i<NEWHOPE_SYMBYTES;i++)
    r[NEWHOPE_POLYBYTES+i] = seed[i];
}

/*************************************************
* Name:        decode_pk
* 
* Description: De-serialize the public key; inverse of encode_pk
*
* Arguments:   poly *pk:               pointer to output public-key polynomial
*              unsigned char *seed:    pointer to output public seed
*              const unsigned char *r: pointer to input byte array
**************************************************/
static void decode_pk(poly *pk, unsigned char *seed, const unsigned char *r)
{
  int i;
  poly_frombytes(pk, r);
  for(i=0;i<NEWHOPE_SYMBYTES;i++)
    seed[i] = r[NEWHOPE_POLYBYTES+i];
}

/*************************************************
* Name:        encode_c
* 
* Description: Serialize the ciphertext as concatenation of the
*              serialization of the polynomial b and serialization
*              of the compressed polynomial v
*
* Arguments:   - unsigned char *r: pointer to the output serialized ciphertext
*              - const poly *b:    pointer to the input polynomial b
*              - const poly *v:    pointer to the input polynomial v
**************************************************/
static void encode_c(unsigned char *r, const poly *b, const poly *v)
{
  poly_tobytes(r,b);
  poly_compress(r+NEWHOPE_POLYBYTES,v);
}

/*************************************************
* Name:        decode_c
* 
* Description: de-serialize the ciphertext; inverse of encode_c
*
* Arguments:   - poly *b:                pointer to output polynomial b
*              - poly *v:                pointer to output polynomial v
*              - const unsigned char *r: pointer to input byte array
**************************************************/
void decode_c(poly *b, poly *v, const unsigned char *r)
{
  poly_frombytes(b, r);
  poly_decompress(v, r+NEWHOPE_POLYBYTES);
}

/*************************************************
* Name:        gen_a
* 
* Description: Deterministically generate public polynomial a from seed
*
* Arguments:   - poly *a:                   pointer to output polynomial a
*              - const unsigned char *seed: pointer to input seed
**************************************************/
static void gen_a(poly *a, const unsigned char *seed)
{
  poly_uniform(a,seed);
}


/*************************************************
* Name:        cpapke_keypair
* 
* Description: Generates public and private key 
*              for the CPA public-key encryption scheme underlying
*              the NewHope KEMs
*
* Arguments:   - unsigned char *pk: pointer to output public key
*              - unsigned char *sk: pointer to output private key
**************************************************/
void cpapke_keypair(unsigned char *pk,
                    unsigned char *sk)
{
  poly ahat, ehat, ahat_shat, bhat, shat;
  poly_quarter s00, s01, s10, s11, s01_s, s10_s, s11_s, e00, e01, e10, e11;
  unsigned char z[2*NEWHOPE_SYMBYTES];
  unsigned char *publicseed = z;
  unsigned char *noiseseed = z+NEWHOPE_SYMBYTES;

  randombytes(z, NEWHOPE_SYMBYTES);
  shake256(z, 2*NEWHOPE_SYMBYTES, z, NEWHOPE_SYMBYTES);
  
  gen_a(&ahat, publicseed);

  poly_sample(&shat, noiseseed, 0); //generate s
  poly_pt_ntt7(&shat, &s00, &s01, &s10, &s11, &s01_s, &s10_s, &s11_s);
  pt_ntt_bowtiemultiply(&ahat_shat, &ahat, &s00, &s01, &s10, &s11, &s01_s, &s10_s, &s11_s);//a*s
   
  poly_sample(&ehat, noiseseed, 1);//geberate e
  poly_pt_ntt4(&ehat, &e00, &e01, &e10, &e11);
  recover_poly(&ehat, &e00, &e01, &e10, &e11);

  poly_add(&bhat, &ehat, &ahat_shat);//a*s+e

  poly_tobytes(sk, &shat);

  encode_pk(pk, &bhat, publicseed);
}

/*************************************************
* Name:        cpapke_enc
* 
* Description: Encryption function of
*              the CPA public-key encryption scheme underlying
*              the NewHope KEMs
*
* Arguments:   - unsigned char *c:          pointer to output ciphertext
*              - const unsigned char *m:    pointer to input message (of length NEWHOPE_SYMBYTES bytes)
*              - const unsigned char *pk:   pointer to input public key
*              - const unsigned char *coin: pointer to input random coins used as seed
*                                           to deterministically generate all randomness
**************************************************/
void cpapke_enc(unsigned char *c,
                const unsigned char *m,
                const unsigned char *pk,
                const unsigned char *coin)
{
  poly sprime, eprime, vprime, vprime1, ahat, bhat, eprimeprime, uhat, v;
  poly_quarter sprime00, sprime01, sprime10, sprime11, sprime01_s, sprime10_s,sprime11_s, eprime00, eprime01, eprime10, eprime11, vprime_0,vprime_1;
  unsigned char publicseed[NEWHOPE_SYMBYTES];

  poly_frommsg(&v, m);

  //generate s', e', e'';
  poly_sample(&sprime, coin, 0);
  poly_sample(&eprime, coin, 1);
  poly_sample(&eprimeprime, coin, 2);

  decode_pk(&bhat, publicseed, pk);
  gen_a(&ahat, publicseed);

  poly_pt_ntt7(&sprime, &sprime00, &sprime01, &sprime10, &sprime11, &sprime01_s, &sprime10_s, &sprime11_s);
  pt_ntt_bowtiemultiply(&uhat,&ahat, &sprime00, &sprime01, &sprime10, &sprime11, &sprime01_s, &sprime10_s, &sprime11_s);//a*s'
  poly_pt_ntt4(&eprime, &eprime00, &eprime01, &eprime10, &eprime11);
  recover_poly(&eprime, &eprime00, &eprime01, &eprime10, &eprime11);
  poly_add(&uhat, &uhat, &eprime);//a*s'+e'

  pt_ntt_bowtiemultiply(&vprime, &bhat, &sprime00, &sprime01, &sprime10, &sprime11, &sprime01_s, &sprime10_s, &sprime11_s);
  poly_inv_ptntt(&vprime);//b*s';
  
  poly_add(&vprime, &vprime, &eprimeprime);//b*s'+e''
  poly_add(&vprime, &vprime, &v); // add message

  encode_c(c, &uhat, &vprime);
}


/*************************************************
* Name:        cpapke_dec
* 
* Description: Decryption function of
*              the CPA public-key encryption scheme underlying
*              the NewHope KEMs
*
* Arguments:   - unsigned char *m:        pointer to output decrypted message
*              - const unsigned char *c:  pointer to input ciphertext
*              - const unsigned char *sk: pointer to input secret key
**************************************************/
void cpapke_dec(unsigned char *m,
                const unsigned char *c,
                const unsigned char *sk)
{
  poly vprime, uhat, tmp, shat, p; //p=u*s
  poly_quarter shat_00, shat_01, shat_10, shat_11, shat_01_s, shat_10_s, shat_11_s, p_0, p_1;
  decode_c(&uhat, &vprime, c);

  poly_frombytes(&shat, sk);
  poly_pt_ntt7(&shat, &shat_00, &shat_01, &shat_10, &shat_11, &shat_01_s, &shat_10_s, &shat_11_s);
  pt_ntt_bowtiemultiply(&p, &uhat, &shat_00, &shat_01, &shat_10, &shat_11, &shat_01_s, &shat_10_s, &shat_11_s);
  poly_inv_ptntt(&p);

  poly_sub(&tmp, &vprime, &p);

  poly_tomsg(m, &tmp);
}
