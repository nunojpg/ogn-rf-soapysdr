/*
    OGN - Open Glider Network - http://glidernet.org/
    Copyright (c) 2015 The OGN Project

    A detailed list of copyright holders can be found in the file "AUTHORS".

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this software.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __BUFFER_H_
#define __BUFFER_H_

#include <unistd.h>

#include <string.h>

#include "fft.h"
#include "r2fft.h"

#include "serialize.h"

// ==================================================================================================

template <class Type>
 class SampleBuffer  // a buffer to hold a batch of samples
{ public:
   static const int HeaderSize = 16;
   union
   { uint32_t Header[HeaderSize];
     struct
     {  int32_t Size;   // allocated size of data
        int32_t Full;   // number of values in the buffer
        int32_t  Len;   // number of values per sample

       uint32_t RxID;   // [id]  Receiver/Antenna ID
       double   Freq;   // [Hz]  RF centre frequency where samples were acquired
       double   Rate;   // [Hz]  sampling rate
       double   Time;   // [sec] time when samples were acquired
       uint32_t Date;   // [sec] integer part of Time to keep precision
       uint32_t Index;  // Sample index/counter
       float BkgNoise;  // Estimate of the background noise
     } ;
   } ;

   Type  *Data;     // (allocated) storage

  public:
   SampleBuffer() { Size=0; Data=0; Full=0; Len=1; RxID=0; Freq=0; Rate=0; Time=0; Date=0; Index=0; BkgNoise=0; }
  ~SampleBuffer() { Free(); }

   void Print(void) const
   { printf("%dx%d(x%d) Rx#%d: %5.3fMHz %5.3fMsps %5.3fs\n", Samples(), Len, sizeof(Type), RxID, 1e-6*Freq, 1e-6*Rate, Samples()/Rate); }

/*
   void Free(void) { if(Data) delete [] Data; Data=0; Size=0; Full=0; }

   int Allocate(int NewSize)
   { if(NewSize<=Size) { Full=0; return Size; } // for timing eficiency: do not reallocate if same or bigger size already allocated
     Free();
     Data = new (std::nothrow) Type [NewSize]; if(Data==0) { Size=0; Full=0; return Size; }
     Size=NewSize; return Size; }
*/
   void Free(void) { if(Data) free(Data); Data=0; Size=0; }

   int Allocate(int NewSize)
   { if(NewSize<=Size) { return Size; } // for eficiency: do not reallocate if same or bigger size already allocated
     // printf("SampleBuffer::Allocate(%d*%d) Size=%d*%d/%d %s\n", NewSize, sizeof(Type), Size, sizeof(Type), Len, Data?" -> ":"NULL");
     Free();
     Data = (Type *)malloc(NewSize*sizeof(Type)); if(Data==0) { Size=0; return Size; }
     Size=NewSize; return Size; }

   int Allocate(int NewLen, int Samples)
   { Len=NewLen; Allocate(NewLen*Samples); return Size; }

   int Samples(void) const { return Full/Len; }                // number of samples
   Type *SamplePtr(int Idx) const { return Data+Idx*Len; }     // pointer to an indexed sample
   Type &operator [](int Idx) { return Data[Idx]; }            // reference to an indexed value

   Type *Sample(int Idx) { return Data + Idx*Len; }

   template <class OtherType>                                  // allocate after another SampleBuffer
    int Allocate(const SampleBuffer<OtherType> &Buffer)
   { Allocate(Buffer.Size);
     Len  = Buffer.Len;
     Rate = Buffer.Rate;
     Date = Buffer.Date;
     Time = Buffer.Time;
     RxID = Buffer.RxID;
     Freq = Buffer.Freq;
     return Size; }

   void Set(Type Value=0)
   { Type *DataPtr=Data; for(int Idx=0; Idx<Size; Idx++) (*DataPtr++)=Value; }

   double Average(void) const
   { double Sum=0;
     for(int Idx=0; Idx<Full; Idx++) Sum+=Data[Idx];
     return Sum/Full; }

   void Crop(int Head, int Tail)                              // crop the buffer itself
   { int32_t NewFull = Full-(Head+Tail)*Len;                  // size of data after the crop
     if(Head)                                                 // if we are to crop from the head
     { memmove(Data, Data+Head*Len, NewFull*sizeof(Type));    // copy/move the data
       Time += Head/Rate;                                     // advance the Time
       Index += Head; }
     Full=NewFull; }

   void Crop(SampleBuffer<Type> &Out, int Head, int Tail)     // copy part of the buffer into a new buffer
   { Out.Allocate(*this);
     int NewFull=Full-(Head+Tail)*Len;
     memmove(Out.Data, Data+Head*Len, NewFull*sizeof(Type));
     Out.Time = Time+Head/Rate;
     Out.Full = NewFull;
     Out.Index = Index+Head;
     Out.BkgNoise = BkgNoise; }

   int Copy(const SampleBuffer<Type> &Buffer)                   // allocate and copy from another SampleBuffer
   { Allocate(Buffer); memcpy(Data, Buffer.Data, Buffer.Full*sizeof(Type)); Full=Buffer.Full; return Full; }
   // { Allocate(Buffer.Size); memcpy(Data, Buffer.Data, Size*sizeof(Type));
   //   Full=Buffer.Full; Len=Buffer.Len; Rate=Buffer.Rate; Time=Buffer.Time; Date=Buffer.Date; Freq=Buffer.Freq; return Size; }

   int CopySample(const SampleBuffer<Type> &Buffer, int Idx)    // copy just one sample (but can be more than one value)
   { Allocate(Buffer->Len);
     Full=Buffer.Len; Len=Buffer.Len; Rate=Buffer.Rate; Time=Buffer.Time+Idx/Rate; Date=Buffer.Date; RxID=Buffer.RxID; Freq=Buffer.Freq;
     memcpy(Data, Buffer.Data + Idx*Len, Len*sizeof(Type));
     return Size; }

   template <class OtherType>
    int CopySampleSum(const SampleBuffer<OtherType> &Buffer)   // copy the sum of all samples
   { return CopySampleSum(Buffer, 0, Buffer.Samples()-1); }

   template <class OtherType>
    int CopySampleSum(SampleBuffer<OtherType> &Buffer, int Idx1, int Idx2) // copy the sum of several samples
   { Allocate(Buffer.Len);
     Full=Buffer.Len; Len=Buffer.Len; Rate=Buffer.Rate; Time=Buffer.Time+0.5*(Idx1+Idx2)/Rate; Date=Buffer.Date; RxID=Buffer.RxID; Freq=Buffer.Freq;
     for(int Idx=0; Idx<Len; Idx++) { Data[Idx]=0; }
     for(int sIdx=Idx1; sIdx<=Idx2; sIdx++)
     { Type *sPtr = Buffer.Data + sIdx*Len;
       for(int Idx=0; Idx<Len; Idx++) { Data[Idx]+=sPtr[Idx]; }
     }
     return Size; }

   template <class ScaleType>
    void operator *= (ScaleType Scale)
   { for(int Idx=0; Idx<Full; Idx++) Data[Idx]*=Scale; }

   int WritePlotFile(const char *FileName, int StartIdx=0, int Values=0) const
   { if(Values==0) Values=Size-StartIdx;
     FILE *File=fopen(FileName, "wt"); if(File==0) return 0;
     fprintf(File, "# %d x %d, Time=%17.6fsec, Freq=%10.6fMHz, Rate=%8.6fMHz\n", Samples(), Len, Date+Time, 1e-6*Freq, 1e-6*Rate);
     for(int Idx=StartIdx; Idx<Full; Idx++)
     { if((Idx-StartIdx)>=Values) break;
       fprintf(File, "%8d: %+12.6f\n", Idx, Data[Idx] ); }
     fclose(File); return Size; }

   int WriteComplexPlotFile(const char *FileName, int StartIdx=0, int Values=0) const
   { if(Values==0) Values=Size-StartIdx;
     FILE *File=fopen(FileName, "wt"); if(File==0) return 0;
     fprintf(File, "# %d x %d, Time=%17.6fsec, Freq=%10.6fMHz, Rate=%8.6fMHz\n", Samples(), Len, Date+Time, 1e-6*Freq, 1e-6*Rate);
     fprintf(File, "# Index    Time [usec]     Real         Imag         Magn  Phase[deg]\n");
     for(int Idx=StartIdx; Idx<Full; Idx++)
     { if((Idx-StartIdx)>=Values) break;
       fprintf(File, "%8d: %12.6f %+12.6f %+12.6f %12.6f %+9.3f\n",
             Idx, 1e6*(Idx-StartIdx)/Rate, real(Data[Idx]), imag(Data[Idx]), sqrt(norm(Data[Idx])), (180/M_PI)*arg(Data[Idx]) ); }
     fclose(File); return Size; }

  template <class StreamType>
   int Serialize(StreamType File) // write SampleBuffer to a file/socket
   { int Total=0, Bytes;
     Bytes=Serialize_WriteData(File, Header, HeaderSize*sizeof(uint32_t)); if(Bytes<0) return -1;
     Total+=Bytes;
/*
     Bytes=Serialize_WriteData(File, &Size, sizeof(int32_t)); if(Bytes<0) return -1;
     Total+=Bytes;
     Bytes=Serialize_WriteData(File, &Full, sizeof(int32_t)); if(Bytes<0) return -1;
     Total+=Bytes;
     Bytes=Serialize_WriteData(File, &Len , sizeof(int32_t)); if(Bytes<0) return -1;
     Total+=Bytes;
     Bytes=Serialize_WriteData(File, &Rate, sizeof(double)); if(Bytes<0) return -1;
     Total+=Bytes;
     Bytes=Serialize_WriteData(File, &Date, sizeof(uint32_t)); if(Bytes<0) return -1;
     Total+=Bytes;
     Bytes=Serialize_WriteData(File, &Time, sizeof(double)); if(Bytes<0) return -1;
     Total+=Bytes;
     Bytes=Serialize_WriteData(File, &RxID, sizeof(uint32_t)); if(Bytes<0) return -1;
     Total+=Bytes;
     Bytes=Serialize_WriteData(File, &Freq, sizeof(double)); if(Bytes<0) return -1;
     Total+=Bytes;
*/
     Bytes=Serialize_WriteData(File,  Data, Full*sizeof(Type)); if(Bytes<0) return -1;
     Total+=Bytes;
     return Total; }

  template <class StreamType>
   int Deserialize(StreamType File)  // read SampleBuffer from a file/socket
   { int Total=0, Bytes; int32_t OldSize=Size;
     Bytes=Serialize_ReadData(File, Header, HeaderSize*sizeof(uint32_t));
     int32_t NewSize=Size; Size=OldSize;
     int32_t NewFull=Full;
     if(Bytes<0) { Full=0; return -1; }
     // printf("SampleBuffer::Deserialize() => %d, Size=%d, NewSize=%d, Full=%d\n", Bytes, Size, NewSize, Full);
     if( (NewSize<0) || (NewSize<Full) ) { Full=0; return -1; }
     Total+=Bytes;
     if(Allocate(NewSize)==0) return -2;
     // printf("SampleBuffer::Deserialize() Size=%d, NewSize=%d, Full=%d\n", Size, NewSize, Full);
     Bytes=Serialize_ReadData(File,  Data, NewFull*sizeof(Type)); if(Bytes<0) return -1;
     Full=NewFull;
     Total+=Bytes;
/*
     int32_t NewSize=0;
     Bytes=Serialize_ReadData(File, &NewSize, sizeof(int32_t)); if(Bytes<0) return -1;
     if(NewSize<0) return -1;
     Total+=Bytes;
     if(Allocate(NewSize)==0) return -2;
     Bytes=Serialize_ReadData(File, &Full, sizeof(int32_t)); if(Bytes<0) return -1;
     Total+=Bytes;
     Bytes=Serialize_ReadData(File, &Len , sizeof(int32_t)); if(Bytes<0) return -1;
     Total+=Bytes;
     Bytes=Serialize_ReadData(File, &Rate, sizeof(double)); if(Bytes<0) return -1;
     Total+=Bytes;
     Bytes=Serialize_ReadData(File, &Date, sizeof(uint32_t)); if(Bytes<0) return -1;
     Total+=Bytes;
     Bytes=Serialize_ReadData(File, &Time, sizeof(double)); if(Bytes<0) return -1;
     Total+=Bytes;
     Bytes=Serialize_ReadData(File, &RxID, sizeof(uint32_t)); if(Bytes<0) return -1;
     Total+=Bytes;
     Bytes=Serialize_ReadData(File, &Freq, sizeof(double)); if(Bytes<0) return -1;
     Total+=Bytes;
     Bytes=Serialize_ReadData(File,  Data, Full*sizeof(Type)); if(Bytes<0) return -1;
     Total+=Bytes;
*/
     return Total; }

   int Write(FILE *File) // write all samples onto a binary file (with header)
   { if(fwrite(Header, HeaderSize*sizeof(uint32_t), 1, File)!=1) return -1;
/*
     if(fwrite(&Size, sizeof(Size), 1, File)!=1) return -1;
     if(fwrite(&Full, sizeof(Full), 1, File)!=1) return -1;
     if(fwrite(&Len,  sizeof(Len),  1, File)!=1) return -1;
     if(fwrite(&Rate, sizeof(Rate), 1, File)!=1) return -1;
     if(fwrite(&Date, sizeof(Date), 1, File)!=1) return -1;
     if(fwrite(&Time, sizeof(Time), 1, File)!=1) return -1;
     if(fwrite(&RxID, sizeof(RxID), 1, File)!=1) return -1;
     if(fwrite(&Freq, sizeof(Freq), 1, File)!=1) return -1;
*/
     if(fwrite(Data,  sizeof(Type), Size, File)!=(size_t)Size) return -1;
     return 1; }

   int Read(FILE *File) // read samples from a binary file (with header)
   { int32_t OldSize=Size;
     if(fread(Header, HeaderSize*sizeof(uint32_t), 1, File)!=1) return -1;
     int32_t NewSize=Size; Size=OldSize;
     int32_t NewFull=Full;
/*
     if(fread(&Size, sizeof(Size), 1, File)!=1) return -1;
     if(fread(&Full, sizeof(Full), 1, File)!=1) return -1;
     if(fread(&Len,  sizeof(Len),  1, File)!=1) return -1;
     if(fread(&Rate, sizeof(Rate), 1, File)!=1) return -1;
     if(fread(&Date, sizeof(Date), 1, File)!=1) return -1;
     if(fread(&Time, sizeof(Time), 1, File)!=1) return -1;
     if(fread(&RxID, sizeof(RxID), 1, File)!=1) return -1;
     if(fread(&Freq, sizeof(Freq), 1, File)!=1) return -1;
*/
     Allocate(NewSize);
     if(fread(Data,  sizeof(Type), NewSize, File)!=(size_t)Size) return -1;
     Size=NewSize; Full=NewFull;
     return 1; }

   int ReadRaw(FILE *File, int Len, int MaxSamples, double Rate=1) // read samples from a raw binary file
   { Allocate(Len, MaxSamples); this->Rate=Rate;
     int Read=fread(Data,  Len*sizeof(Type), MaxSamples, File);
     Full=Len*Read; return Full; }

   int ReadRaw(const char *FileName, int Len, int MaxSamples, double Rate=1)
   { FILE *File=fopen(FileName, "rb"); if(!File) return -1;
     int Ret=ReadRaw(File, Len, MaxSamples, Rate);
     fclose(File); return Ret; }

   int WriteRaw(FILE *File) const
   { return fwrite(Data,  sizeof(Type), Full, File); }

   int WriteRaw(const char *FileName) const
   { FILE *File=fopen(FileName, "wb");
     int Ret=WriteRaw(File);
     fclose(File); return Ret; }
} ;

// ==================================================================================================

// Note 1: the sliding FFT routines below take sliding step = half the FFT window size (thus SineWindow should be used)
// Note 2: the FFT output spectra have the two halfs swapped around thus the FFT amplitude corresponding to the center frequency is in the middle

template <class Float>
 int SlidingFFT(SampleBuffer< std::complex<Float> > &Output, const SampleBuffer< std::complex<uint8_t> > &Input,
                InpSlideFFT<Float> &FFT, Float InpBias=127.38)
{ return SlidingFFT(Output, Input, FFT.FwdFFT, FFT.Window, InpBias); }

template <class Float> // do sliding FFT over a buffer of (complex 8-bit) samples, produce (float/double complex) spectra
 int SlidingFFT(SampleBuffer< std::complex<Float> > &Output, const SampleBuffer< std::complex<uint8_t> >&Input,
                DFT1d<Float> &FwdFFT, Float *Window, Float InpBias=127.38)
{ 
  return 0; }

template <class Float>
 int SlidingFFT(SampleBuffer< std::complex<Float> > &Output, const SampleBuffer<uint8_t> &Input,
                InpSlideFFT<Float> &FFT, Float InpBias=127.38)
{ return SlidingFFT(Output, Input, FFT.FwdFFT, FFT.Window, InpBias); }

template <class Float> // do sliding FFT over a buffer of (complex 8-bit) samples, produce (float/double complex) spectra
 int SlidingFFT(SampleBuffer< std::complex<Float> > &Output, const SampleBuffer<uint8_t> &Input,
                DFT1d<Float> &FwdFFT, Float *Window, Float InpBias=127.38)
{ int WindowSize = FwdFFT.Size;                                                        // FFT object and Window shape are prepared already
  int WindowSize2=WindowSize/2;                                                        // Slide step
  int InpSamples=Input.Full/2;                                                         // number of complex,8-bit input samples
  // printf("SlidingFFT() %d point FFT, %d input samples\n", FwdFFT.Size, InpSamples);
  Output.Allocate((InpSamples/WindowSize2+1)*WindowSize); Output.Len=WindowSize;         // output is rows of spectral data
  Output.Rate = Input.Rate/WindowSize2;
  Output.Time = Input.Time;
  Output.Date = Input.Date;
  Output.RxID = Input.RxID;
  Output.Freq = Input.Freq;
  uint8_t *InpData = Input.Data;
  std::complex<Float> *OutData = Output.Data;
  int Slides=0;
  { std::complex<Float> *Buffer = FwdFFT.Buffer;                  // first slide is special
    for( int Bin=0; Bin<WindowSize2; Bin++) { Buffer[Bin] = 0; }    // half the window is empty
    for( int Bin=WindowSize2; Bin<WindowSize; Bin++)                // the other half contains the first input samples
    { Buffer[Bin] = std::complex<float>( Window[Bin]*(InpData[0]-InpBias), Window[Bin]*(InpData[1]-InpBias) );
      InpData+=2; }
    FwdFFT.Execute();                                             // execute FFT
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;  // copy spectra into the output buffer
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;  // swap around the two halfs
    InpData-=2*WindowSize2; Slides++; }
  for( ; InpSamples>=WindowSize; InpSamples-=WindowSize2)           // now the following slides
  { std::complex<Float> *Buffer = FwdFFT.Buffer;
    for( int Bin=0; Bin<WindowSize; Bin++)
    { Buffer[Bin] = std::complex<float>( Window[Bin]*(InpData[0]-InpBias), Window[Bin]*(InpData[1]-InpBias) );
      InpData+=2; }
    FwdFFT.Execute();
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    InpData-=2*WindowSize2; Slides++; }
  { std::complex<Float> *Buffer = FwdFFT.Buffer;                  // and the last slide: special
    for( int Bin=0; Bin<WindowSize2; Bin++)
    { Buffer[Bin] = std::complex<float>( Window[Bin]*(InpData[0]-InpBias), Window[Bin]*(InpData[1]-InpBias) );
      InpData+=2; }
    for( int Bin=WindowSize2; Bin<WindowSize; Bin++)
    { Buffer[Bin] = 0; }
    FwdFFT.Execute();
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    InpData-=2*WindowSize2; Slides++; }

  Output.Full=Slides*WindowSize;
  return Slides; }

// --------------------------------------------------------------------------------------------------
/*
template <class Float> // do sliding FFT over a buffer of (complex 16-bit) samples, produce (float/double complex) spectra
 int SlidingFFT(SampleBuffer< std::complex<Float> > &Output, SampleBuffer<int16_t> &Input,
                DFT1d<Float> &FwdFFT, Float *Window)
{ int WindowSize = FwdFFT.Size;                                                        // FFT object and Window shape are prepared already
  int WindowSize2=WindowSize/2;                                                        // Slide step
  int InpSamples=Input.Full/2;                                                         // number of complex,8-bit input samples
  // printf("SlidingFFT() %d point FFT, %d input samples\n", FwdFFT.Size, InpSamples);
  Output.Allocate((InpSamples/WindowSize2+1)*WindowSize); Output.Len=WindowSize;       // output is rows of spectral data
  Output.Rate=Input.Rate/WindowSize2; Output.Time=Input.Time; Output.Date=Input.Date; Output.Freq=Input.Freq;
  int16_t *InpData = Input.Data;
  std::complex<Float> *OutData = Output.Data;
  int Slides=0;
  { std::complex<Float> *Buffer = FwdFFT.Buffer;                                      // first slide is special
    for( int Bin=0; Bin<WindowSize2; Bin++) { Buffer[Bin] = 0; }                      // half the window is empty
    for( int Bin=WindowSize2; Bin<WindowSize; Bin++)                                  // the other half contains the first input samples
    { Buffer[Bin] = std::complex<float>( Window[Bin]*InpData[0], Window[Bin]*InpData[1] );
      InpData+=2; }
    FwdFFT.Execute();                                                                 // execute FFT
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;  // copy spectra into the output buffer
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;  // swap around the two halfs
    InpData-=2*WindowSize2; Slides++; }
  for( ; InpSamples>=WindowSize; InpSamples-=WindowSize2)                             // now the following slides
  { std::complex<Float> *Buffer = FwdFFT.Buffer;
    for( int Bin=0; Bin<WindowSize; Bin++)
    { Buffer[Bin] = std::complex<float>( Window[Bin]*InpData[0], Window[Bin]*InpData[1] );
      InpData+=2; }
    FwdFFT.Execute();
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    InpData-=2*WindowSize2; Slides++; }
  { std::complex<Float> *Buffer = FwdFFT.Buffer;                  // and the last slide: special
    for( int Bin=0; Bin<WindowSize2; Bin++)
    { Buffer[Bin] = std::complex<float>( Window[Bin]*InpData[0], Window[Bin]*InpData[1] );
      InpData+=2; }
    for( int Bin=WindowSize2; Bin<WindowSize; Bin++)
    { Buffer[Bin] = 0; }
    FwdFFT.Execute();
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    InpData-=2*WindowSize2; Slides++; }

  Output.Full=Slides*WindowSize;
  return Slides; }
*/
// --------------------------------------------------------------------------------------------------

// separate the result of a two real channels FFT
template <class BuffType, class DataType>
 void SeparTwoReals(BuffType *Buff, DataType *Out0, DataType *Out1, int Size)
{ int idx,HalfSize=Size/2;
  Out0[0].real(Buff[0].real());
  Out1[0].real(Buff[0].imag());
  for(idx=1; idx<HalfSize; idx++)
  { Out0[idx].real ( Buff[idx].real() +Buff[Size-idx].real());
    Out0[idx].imag ( Buff[idx].imag() -Buff[Size-idx].imag());
    Out1[idx].real ( Buff[idx].imag() +Buff[Size-idx].imag());
    Out1[idx].imag (-Buff[idx].real() +Buff[Size-idx].real()); }
  Out0[0].imag(Buff[HalfSize].real());
  Out1[0].imag(Buff[HalfSize].imag());
}

template <class Float> // do sliding FFT over a buffer of (real signed 16-bit) samples, produce (float/double complex) spectra
 int SlidingFFT(SampleBuffer< std::complex<Float> > &Output, const SampleBuffer<int16_t> &Input,
                DFT1d<Float> &FwdFFT, Float *Window, Float InpBias=0)
{ int WindowSize = FwdFFT.Size;                                                        // FFT object and Window shape are prepared already
  int WindowSize2=WindowSize/2;                                                        // Slide step
  int InpSamples=Input.Full;                                                           // number of real, 16-bit input samples
  // printf("SlidingFFT() %d point FFT, %d input samples\n", FwdFFT.Size, InpSamples);
  Output.Allocate((InpSamples/WindowSize2+2)*WindowSize2); Output.Len=WindowSize2;     // output is rows of spectral data
  Output.Rate = Input.Rate/WindowSize2;
  Output.Time = Input.Time;
  Output.Date = Input.Date;
  Output.RxID = Input.RxID;
  Output.Freq = Input.Freq; // +Input.Rate/4;
  int16_t *InpData = Input.Data;
  std::complex<Float> *OutData = Output.Data;
  int Slides=0;
  { std::complex<Float> *Buffer = FwdFFT.Buffer;                        // first slide is special
    for( int Bin=0; Bin<WindowSize2; Bin++) { Buffer[Bin].real(0); }    // half the window is empty
    for( int Bin=WindowSize2; Bin<WindowSize; Bin++)                    // the other half contains the first input samples
    { Buffer[Bin].real(Window[Bin]*(InpData[Bin-WindowSize2]-InpBias)); }
    Slides++;                                                           // 1st slide done
    for( int Bin=0; Bin<WindowSize; Bin++)                              // 2nd slide into the imag. part
    { Buffer[Bin].imag(Window[Bin]*(InpData[Bin]-InpBias)); }
    InpData+=WindowSize2; InpSamples-=WindowSize2; Slides++;            // count slides and input samples
    FwdFFT.Execute();                                                   // execute FFT
    SeparTwoReals(Buffer, OutData, OutData+WindowSize2, WindowSize);    // copy spectra into the output buffer
    OutData+=WindowSize; }
  for( ; InpSamples>=(2*WindowSize); InpSamples-=WindowSize)            // now the following slides
  { std::complex<Float> *Buffer = FwdFFT.Buffer;
    // for( int Bin=0; Bin<WindowSize; Bin++)                              // one window in real part of the FFT input
    // { Buffer[Bin] = Window[Bin]*std::complex<Float>(InpData[Bin]-InpBias, InpData[WindowSize2+Bin]-InpBias); }
    // InpData+=WindowSize; Slides+=2;
    for( int Bin=0; Bin<WindowSize; Bin++)                              // one window in real part of the FFT input
    { Buffer[Bin].real(Window[Bin]*(InpData[Bin]-InpBias)); }
    InpData+=WindowSize2; Slides++;
    for( int Bin=0; Bin<WindowSize; Bin++)                              // other window in the imaginary part of the FFT input
    { Buffer[Bin].imag(Window[Bin]*(InpData[Bin]-InpBias)); }
    InpData+=WindowSize2; Slides++;
    // double InpSumR=0; double InpSumI=0;
    // for( int Bin=0; Bin<WindowSize; Bin++)
    // { InpSumR += Buffer[Bin].real()*Buffer[Bin].real();
    //   InpSumI += Buffer[Bin].imag()*Buffer[Bin].imag(); }
    // InpSumR/=WindowSize; InpSumI/=WindowSize;
    FwdFFT.Execute();
    // double OutSumR=0; double OutSumI=0;
    // for( int Bin=0; Bin<WindowSize; Bin++)
    // { OutSumR += Buffer[Bin].real()*Buffer[Bin].real();
    //   OutSumI += Buffer[Bin].imag()*Buffer[Bin].imag(); }
    // OutSumR/=WindowSize; OutSumI/=WindowSize;
    // printf("SlidingFFT() InpSamples=%d Slides=%d, InpSumR/I=%5.3f/%5.3f. OutSumR/I=%5.3f/%5.3f\n", InpSamples, Slides, sqrt(InpSumR), sqrt(InpSumI), sqrt(OutSumR), sqrt(OutSumI));
    SeparTwoReals(Buffer, OutData, OutData+WindowSize2, WindowSize);    // copy spectra into the output buffer
    OutData+=WindowSize; }
  { std::complex<Float> *Buffer = FwdFFT.Buffer;
    int Bin;
    for( Bin=0; Bin<WindowSize; Bin++)
    { if(Bin>=InpSamples) break;
      Buffer[Bin].real(Window[Bin]*(InpData[Bin]-InpBias)); }
    for(      ; Bin<WindowSize; Bin++)
    { Buffer[Bin].real(0); }
    InpData+=WindowSize2; Slides++; InpSamples-=WindowSize2; if(InpSamples<0) InpSamples=0;
    for( Bin=0; Bin<WindowSize; Bin++)
    { if(Bin>=InpSamples) break;
      Buffer[Bin].imag(Window[Bin]*(InpData[Bin]-InpBias)); }
    for(      ; Bin<WindowSize; Bin++)
    { Buffer[Bin].imag(0); }
    InpData+=WindowSize2; Slides++; InpSamples-=WindowSize2; if(InpSamples<0) InpSamples=0;
    FwdFFT.Execute();
    SeparTwoReals(Buffer, OutData, OutData+WindowSize2, WindowSize);    // copy spectra into the output buffer
    OutData+=WindowSize; }
  Output.Full=Slides*WindowSize2;
  return Slides; }

// --------------------------------------------------------------------------------------------------

template <class Float> // do sliding FFT over a buffer of int16_t complex samples, produce (float/double complex) spectra
 int SlidingFFT(SampleBuffer< std::complex<Float> > &Output, const SampleBuffer< std::complex<int16_t> > &Input,
                DFT1d<Float> &FwdFFT, Float *Window)
{ int WindowSize = FwdFFT.Size;                                                        // FFT object and Window shape are prepared already
  int WindowSize2=WindowSize/2;                                                        // Slide step
  int InpSamples=Input.Full;                                                           // number of complex float/double samples
  // printf("SlidingFFT() %d point FFT, %d input samples\n", FwdFFT.Size, InpSamples);
  Output.Allocate((InpSamples/WindowSize2+1)*WindowSize); Output.Len=WindowSize;         // output is rows of spectral data
  Output.Rate = Input.Rate/WindowSize2;
  Output.Time = Input.Time;
  Output.Date = Input.Date;
  Output.RxID = Input.RxID;
  Output.Freq = Input.Freq;
  std::complex<int16_t> *InpData = Input.Data;
  std::complex<Float> *OutData = Output.Data;
  int Slides=0;
  { std::complex<Float> *Buffer = FwdFFT.Buffer;                  // first slide is special
    for( int Bin=0; Bin<WindowSize2; Bin++) { Buffer[Bin] = 0; }    // half the window is empty
    for( int Bin=WindowSize2; Bin<WindowSize; Bin++)                // the other half contains the first input samples
    { Buffer[Bin] = std::complex<Float>(Window[Bin]*real(InpData[Bin-WindowSize2]), Window[Bin]*imag(InpData[Bin-WindowSize2])); }
    FwdFFT.Execute();                                             // execute FFT
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;  // copy spectra into the output buffer
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;  // swap around the two halfs
    Slides++; }
  for( ; InpSamples>=WindowSize; InpSamples-=WindowSize2)           // now the following slides
  { std::complex<Float> *Buffer = FwdFFT.Buffer;
    for( int Bin=0; Bin<WindowSize; Bin++)
    { Buffer[Bin] = std::complex<Float>(Window[Bin]*real(InpData[Bin]), Window[Bin]*imag(InpData[Bin])); }
    FwdFFT.Execute();
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    InpData+=WindowSize2; Slides++; }
  { std::complex<Float> *Buffer = FwdFFT.Buffer;                  // and the last slide: special
    for( int Bin=0; Bin<WindowSize2; Bin++)
    { Buffer[Bin] = std::complex<Float>(Window[Bin]*real(InpData[Bin]), Window[Bin]*imag(InpData[Bin])); }
    for( int Bin=WindowSize2; Bin<WindowSize; Bin++)
    { Buffer[Bin] = 0; }
    FwdFFT.Execute();
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    InpData+=WindowSize2; Slides++; }

  Output.Full=Slides*WindowSize;
  return Slides; }

// --------------------------------------------------------------------------------------------------

template <class Float> // do sliding FFT over a buffer of float/double complex samples, produce (float/double complex) spectra
 int SlidingFFT(SampleBuffer< std::complex<Float> > &Output, const SampleBuffer< std::complex<Float> > &Input,
                DFT1d<Float> &FwdFFT, Float *Window)
{ int WindowSize = FwdFFT.Size;                                                        // FFT object and Window shape are prepared already
  int WindowSize2=WindowSize/2;                                                        // Slide step
  int InpSamples=Input.Full;                                                           // number of complex float/double samples
  // printf("SlidingFFT() %d point FFT, %d input samples\n", FwdFFT.Size, InpSamples);
  Output.Allocate((InpSamples/WindowSize2+1)*WindowSize); Output.Len=WindowSize;         // output is rows of spectral data
  Output.Rate = Input.Rate/WindowSize2;
  Output.Time = Input.Time;
  Output.Date = Input.Date;
  Output.RxID = Input.RxID;
  Output.Freq = Input.Freq;
  std::complex<Float> *InpData = Input.Data;
  std::complex<Float> *OutData = Output.Data;
  int Slides=0;
  { std::complex<Float> *Buffer = FwdFFT.Buffer;                  // first slide is special
    for( int Bin=0; Bin<WindowSize2; Bin++) { Buffer[Bin] = 0; }    // half the window is empty
    for( int Bin=WindowSize2; Bin<WindowSize; Bin++)                // the other half contains the first input samples
    { Buffer[Bin] = Window[Bin]*InpData[Bin-WindowSize2]; }
    FwdFFT.Execute();                                             // execute FFT
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;  // copy spectra into the output buffer
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;  // swap around the two halfs
    Slides++; }
  for( ; InpSamples>=WindowSize; InpSamples-=WindowSize2)           // now the following slides
  { std::complex<Float> *Buffer = FwdFFT.Buffer;
    for( int Bin=0; Bin<WindowSize; Bin++)
    { Buffer[Bin] = Window[Bin]*InpData[Bin]; }
    FwdFFT.Execute();
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    InpData+=WindowSize2; Slides++; }
  { std::complex<Float> *Buffer = FwdFFT.Buffer;                  // and the last slide: special
    for( int Bin=0; Bin<WindowSize2; Bin++)
    { Buffer[Bin] = Window[Bin]*InpData[Bin]; }
    for( int Bin=WindowSize2; Bin<WindowSize; Bin++)
    { Buffer[Bin] = 0; }
    FwdFFT.Execute();
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    InpData+=WindowSize2; Slides++; }

  Output.Full=Slides*WindowSize;
  return Slides; }

template <class Float> // do sliding FFT over a buffer of float/double complex samples, produce (float/double complex) spectra
 int ReconstrFFT(SampleBuffer< std::complex<Float> > &Output, const SampleBuffer< std::complex<Float> > &Input,
                 DFT1d<Float> &InvFFT, Float *Window)
{ int WindowSize = InvFFT.Size;                                                        // FFT object and Window shape are prepared already
  int WindowSize2=WindowSize/2;                                                        // Slide step
  int InpSlides=Input.Samples();                                                       //
  // printf("ReconstrFFT() %d point FFT, %d input samples\n", FwdFFT.Size, InpSlides);
  Output.Allocate(1, (InpSlides+1)*WindowSize2);                                     // output is complex time-linear samples
  Output.Rate = Input.Rate*WindowSize2;
  Output.Time = Input.Time-1.0/Input.Rate;
  Output.Date = Input.Date;
  Output.RxID = Input.RxID;
  Output.Freq = Input.Freq;
  std::complex<Float> *InpData = Input.Data;
  std::complex<Float> *OutData = Output.Data;
  int Slides=0;
  { std::complex<Float> *Buffer = InvFFT.Buffer;
    memcpy(Buffer+WindowSize2, InpData, WindowSize2*sizeof(std::complex<Float>)); InpData+=WindowSize2;  // copy spectra into the output buffer
    memcpy(Buffer,             InpData, WindowSize2*sizeof(std::complex<Float>)); InpData+=WindowSize2;  // swap around the two halfs
    InvFFT.Execute();
    for(int Idx=0; Idx<WindowSize; Idx++)
    { OutData[Idx]=Window[Idx]*Buffer[Idx]; }
    OutData+=WindowSize2; Slides++; InpSlides--; }
  for( ; InpSlides; )
  { std::complex<Float> *Buffer = InvFFT.Buffer;
    memcpy(Buffer+WindowSize2, InpData, WindowSize2*sizeof(std::complex<Float>)); InpData+=WindowSize2;  // copy spectra into the output buffer
    memcpy(Buffer,             InpData, WindowSize2*sizeof(std::complex<Float>)); InpData+=WindowSize2;  // swap around the two halfs
    InvFFT.Execute();
    for(int Idx=0; Idx<WindowSize2; Idx++)
    { OutData[Idx]+=Window[Idx]*Buffer[Idx]; }
    for(int Idx=WindowSize2; Idx<WindowSize; Idx++)
    { OutData[Idx]=Window[Idx]*Buffer[Idx]; }
    OutData+=WindowSize2; Slides++; InpSlides--; }

  Output.Full=(Slides+1)*WindowSize2;
  return Slides; }

// ==================================================================================================
// Sliding FFT with r2FFT (no open-source restrictions)

template <class Float> // do sliding FFT over a buffer of float/double complex samples, produce (float/double complex) spectra
 int SlidingFFT(SampleBuffer< std::complex<Float> > &Output, const SampleBuffer< std::complex<Float> > &Input,
                r2FFT<Float> &FFT, Float *Window, std::complex<Float> *Buffer)
{ int WindowSize = FFT.Size;                                                        // FFT object and Window shape are prepared already
  int WindowSize2=WindowSize/2;                                                        // Slide step
  int InpSamples=Input.Full;                                                           // number of complex float/double samples
  // printf("SlidingFFT() %d point FFT, %d input samples\n", FFT.Size, InpSamples);
  Output.Allocate((InpSamples/WindowSize2+1)*WindowSize); Output.Len=WindowSize;         // output is rows of spectral data
  Output.Rate = Input.Rate/WindowSize2;
  Output.Time = Input.Time;
  Output.Date = Input.Date;
  Output.RxID = Input.RxID;
  Output.Freq = Input.Freq;
  std::complex<Float> *InpData = Input.Data;
  std::complex<Float> *OutData = Output.Data;
  int Slides=0;
  {                                                                 // first slide is special
    for( int Bin=0; Bin<WindowSize2; Bin++) { Buffer[Bin] = 0; }    // half the window is empty
    for( int Bin=WindowSize2; Bin<WindowSize; Bin++)                // the other half contains the first input samples
    { Buffer[Bin] = Window[Bin]*InpData[Bin-WindowSize2]; }
    FFT.Process(Buffer);                                            // execute FFT
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;  // copy spectra into the output buffer
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;  // swap around the two halfs
    Slides++; }
  for( ; InpSamples>=WindowSize; InpSamples-=WindowSize2)           // now the following slides
  {
    for( int Bin=0; Bin<WindowSize; Bin++)
    { Buffer[Bin] = Window[Bin]*InpData[Bin]; }
    FFT.Process(Buffer);
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    InpData+=WindowSize2; Slides++; }
  {                                                                // and the last slide: special
    for( int Bin=0; Bin<WindowSize2; Bin++)
    { Buffer[Bin] = Window[Bin]*InpData[Bin]; }
    for( int Bin=WindowSize2; Bin<WindowSize; Bin++)
    { Buffer[Bin] = 0; }
    FFT.Process(Buffer);
    memcpy(OutData, Buffer+WindowSize2, WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    memcpy(OutData, Buffer,             WindowSize2*sizeof(std::complex<Float>)); OutData+=WindowSize2;
    InpData+=WindowSize2; Slides++; }

  Output.Full=Slides*WindowSize;
  return Slides; }

template <class Float> // do sliding FFT over a buffer of float/double complex samples, produce (float/double complex) spectra
 int ReconstrFFT(SampleBuffer< std::complex<Float> > &Output, const SampleBuffer< std::complex<Float> > &Input,
                 r2FFT<Float> &FFT, Float *Window, std::complex<Float> *Buffer)
{ int WindowSize = FFT.Size;                                                           // FFT object and Window shape are prepared already
  int WindowSize2=WindowSize/2;                                                        // Slide step
  int InpSlides=Input.Samples();                                                       //
  // printf("ReconstrFFT() %d point FFT, %d input samples\n", FwdFFT.Size, InpSlides);
  Output.Allocate(1, (InpSlides+1)*WindowSize2);                                     // output is complex time-linear samples
  Output.Rate = Input.Rate*WindowSize2;
  Output.Time = Input.Time-1.0/Input.Rate;
  Output.Date = Input.Date;
  Output.RxID = Input.RxID;
  Output.Freq = Input.Freq;
  std::complex<Float> *InpData = Input.Data;
  std::complex<Float> *OutData = Output.Data;
  int Slides=0;
  {
    // memcpy(Buffer+WindowSize2, InpData, WindowSize2*sizeof(std::complex<Float>)); InpData+=WindowSize2;  // copy spectra into the output buffer
    // memcpy(Buffer,             InpData, WindowSize2*sizeof(std::complex<Float>)); InpData+=WindowSize2;  // swap around the two halfs
    for(int Idx=0; Idx<WindowSize2; Idx++)
    { Buffer[WindowSize2+Idx] = conj(InpData[Idx]); }
    InpData+=WindowSize2;
    for(int Idx=0; Idx<WindowSize2; Idx++)
    { Buffer[            Idx] = conj(InpData[Idx]); }
    InpData+=WindowSize2;
    FFT.Process(Buffer);
    for(int Idx=0; Idx<WindowSize; Idx++)
    { OutData[Idx]=Window[Idx]*conj(Buffer[Idx]); }
    OutData+=WindowSize2; Slides++; InpSlides--; }
  for( ; InpSlides; )
  {
    // memcpy(Buffer+WindowSize2, InpData, WindowSize2*sizeof(std::complex<Float>)); InpData+=WindowSize2;  // copy spectra into the output buffer
    // memcpy(Buffer,             InpData, WindowSize2*sizeof(std::complex<Float>)); InpData+=WindowSize2;  // swap around the two halfs
    for(int Idx=0; Idx<WindowSize2; Idx++)
    { Buffer[WindowSize2+Idx] = conj(InpData[Idx]); }
    InpData+=WindowSize2;
    for(int Idx=0; Idx<WindowSize2; Idx++)
    { Buffer[            Idx] = conj(InpData[Idx]); }
    InpData+=WindowSize2;
    FFT.Process(Buffer);
    for(int Idx=0; Idx<WindowSize2; Idx++)
    { OutData[Idx]+=Window[Idx]*conj(Buffer[Idx]); }
    for(int Idx=WindowSize2; Idx<WindowSize; Idx++)
    { OutData[Idx]=Window[Idx]*conj(Buffer[Idx]); }
    OutData+=WindowSize2; Slides++; InpSlides--; }

  Output.Full=(Slides+1)*WindowSize2;
  return Slides; }

// ==================================================================================================

#ifdef USE_RPI_GPU_FFT

// template <class Float> // do sliding FFT over a buffer of (complex 8-bit) samples, produce (float/double complex) spectra
 int SlidingFFT(SampleBuffer< std::complex<float> > &Output, const SampleBuffer<uint8_t> &Input,
                RPI_GPU_FFT &FwdFFT, float *Window, float InpBias=127.38)
{ int Jobs = FwdFFT.Jobs;
  int WindowSize = FwdFFT.Size;                                                        // FFT object and Window shape are prepared already
  int WindowSize2=WindowSize/2;                                                          // Slide step
  int InpSamples=Input.Full/2;                                                         // number of complex,8-bit input samples
  // printf("SlidingFFT(RPI_GPU_FFT) %d point FFT, %d jobs/GPU, %d input samples\n", FwdFFT.Size, Jobs, InpSamples);
  Output.Allocate((InpSamples/WindowSize2+1)*WindowSize); Output.Len=WindowSize;         // output is rows of spectral data
  Output.Rate = Input.Rate/WindowSize2;
  Output.Time = Input.Time;
  Output.Date = Input.Date;
  Output.RxID = Input.RxID;
  Output.Freq = Input.Freq;
  uint8_t *InpData = Input.Data;
  std::complex<float> *OutData = Output.Data;
  int Slides=0; int Job=0;
  { std::complex<float> *Buffer = FwdFFT.Input(Job);                // first slide is special
    for( int Bin=0; Bin<WindowSize2; Bin++) { Buffer[Bin] = 0; }    // half the window is empty
    for( int Bin=WindowSize2; Bin<WindowSize; Bin++)                // the other half contains the first input samples
    { Buffer[Bin] = std::complex<float>( Window[Bin]*(InpData[0]-InpBias), Window[Bin]*(InpData[1]-InpBias) );
      InpData+=2; }
    Job++; InpData-=2*WindowSize2; }
  for( ; InpSamples>=WindowSize; InpSamples-=WindowSize2)           // now the following slides
  { std::complex<float> *Buffer = FwdFFT.Input(Job);
    for( int Bin=0; Bin<WindowSize; Bin++)
    { Buffer[Bin] = std::complex<float>( Window[Bin]*(InpData[0]-InpBias), Window[Bin]*(InpData[1]-InpBias) );
      InpData+=2; }
    Job++; InpData-=2*WindowSize2;
    if(Job>=Jobs)
    { FwdFFT.Execute();
      for(int J=0; J<Jobs; J++)
      { memcpy(OutData, FwdFFT.Output(J)+WindowSize2, WindowSize2*sizeof(std::complex<float>)); OutData+=WindowSize2;
        memcpy(OutData, FwdFFT.Output(J),             WindowSize2*sizeof(std::complex<float>)); OutData+=WindowSize2; }
      Slides+=Jobs; Job=0;
    }
  }
  { std::complex<float> *Buffer = FwdFFT.Input(Job);                  // and the last slide: special
    for( int Bin=0; Bin<WindowSize2; Bin++)
    { Buffer[Bin] = std::complex<float>( Window[Bin]*(InpData[0]-InpBias), Window[Bin]*(InpData[1]-InpBias));
      InpData+=2; }
    for( int Bin=WindowSize2; Bin<WindowSize; Bin++)
    { Buffer[Bin] = 0; }
    Job++; InpData-=2*WindowSize2;
    { FwdFFT.Execute();
      for(int J=0; J<Job; J++)
      { memcpy(OutData, FwdFFT.Output(J)+WindowSize2, WindowSize2*sizeof(std::complex<float>)); OutData+=WindowSize2;
        memcpy(OutData, FwdFFT.Output(J),             WindowSize2*sizeof(std::complex<float>)); OutData+=WindowSize2; }
      Slides+=Job; Job=0;
    }
  }

  // printf("SlidingFFT(RPI_GPU_FFT) %d slides\n", Slides);
  Output.Full=Slides*WindowSize;
  return Slides; }

#endif

// ==================================================================================================

template <class Float>
 inline Float Power(Float *X)
{ Float Re=X[0]; Float Im=X[1]; return Re*Re+Im*Im; }

template <class Float>
 inline Float Power(std::complex<Float> &X)
{ Float Re=real(X); Float Im=imag(X); return Re*Re+Im*Im; }

template <class Float>  // convert (complex) spectra to power (energy)
 void SpectraPower(SampleBuffer<Float> &Output, SampleBuffer< std::complex<Float> > &Input)
{ Output.Allocate(Input); int WindowSize=Input.Len;
  std::complex<Float> *InpData=Input.Data;
  Float *OutData=Output.Data;
  int Slides=Input.Full/Input.Len;
  for( int Slide=0; Slide<Slides; Slide++)
  { for( int Bin=0; Bin<WindowSize; Bin++)
    { OutData[Bin] = Power(InpData[Bin]); }
    InpData+=WindowSize; OutData+=WindowSize; }
  Output.Full=Input.Full; }

template <class Float>  // convert (complex) spectra to power (energy) - at same time calc. the average spectra power
 Float SpectraPower(SampleBuffer<Float> &Output, SampleBuffer< std::complex<Float> > &Input, int LowBin, int Bins)
{ int WindowSize=Input.Len;
  int Slides=Input.Full/WindowSize;
  Output.Allocate(Bins,Slides);
  Float *OutData=Output.Data;
  double Sum=0;
  for( int Slide=0; Slide<Slides; Slide++)
  { std::complex<Float> *InpData=Input.Data+(Slide*WindowSize+LowBin);
    for( int Bin=0; Bin<Bins; Bin++)
    { Sum += OutData[Bin] = Power(InpData[Bin]); }
    OutData+=Bins; }
  Output.Full=Bins*Slides;
  return Sum/Output.Full; }

template <class Float>
 Float SpectraPowerLogHist(int *LogHist, SampleBuffer<Float> &Power, Float Median)
{ Float Thres[3];
  Thres[0]=Median; Thres[1]=2*Median; Thres[2]=4*Median;
  LogHist[0]=0;    LogHist[1]=0;      LogHist[2]=0;      LogHist[3]=0;
  for(int Idx=0; Idx<Power.Full; Idx++)
  { Float Pwr=Power.Data[Idx];
    if(Pwr<Thres[0]) { LogHist[0]++; continue; }
    if(Pwr<Thres[1]) { LogHist[1]++; continue; }
    if(Pwr<Thres[2]) { LogHist[2]++; continue; }
    LogHist[3]++;
  }
  if(LogHist[1]==0) return 0;
  return -Median/log((double)LogHist[1]/LogHist[0]); } // return estimated sigma of the noise

template <class Float>
 Float SpectraPowerLogHist(SampleBuffer<Float> &Power, Float Median)
{ int LogHist[4]; return SpectraPowerLogHist(LogHist, Power, Median); }

template <class Float>
 Float SpectraPowerLogHist(int *LogHist, SampleBuffer<Float> &Power, Float Median, int HistSize)
{ Float Thres[HistSize-1];
  LogHist[0]=0; Thres[0]=Median;
  for(int Bin=1; Bin<(HistSize-1); Bin++)
  { LogHist[Bin]=0; Thres[Bin]=2*Thres[Bin-1]; }
  LogHist[HistSize-1]=0;
  for(int Idx=0; Idx<Power.Full; Idx++)
  { Float Pwr=Power.Data[Idx];
    int Bin;
    for(Bin=0; Bin<(HistSize-1); Bin++)
    { if(Pwr<Thres[Bin]) { LogHist[Bin]+=1; break; } }
    if(Bin==(HistSize-1)) LogHist[HistSize-1]+=1;
  }
  if(LogHist[1]==0) return 0;
  return -Median/log((double)LogHist[1]/LogHist[0]); } // return estimated sigma of the noise

template <class Float>
 Float SpectraPowerLogHist(SampleBuffer<Float> &Power, Float Median, int HistSize)
{ int LogHist[HistSize]; return SpectraPowerLogHist(LogHist, Power, Median, HistSize); }

// ==================================================================================================

template <class Float>       // write an image (.pgm) spectrogram file out of the spectra power data
 int Spectrogram(const Float *Power, int Slides, int SpectraSize, const char *ImageFileName, Float RefPwr=1.00)
{
  FILE *ImageFile=0; if(ImageFileName) ImageFile=fopen(ImageFileName, "wb");
  if(ImageFile==0) return -1;
  fprintf(ImageFile, "P5\n%5d %6d\n255\n", SpectraSize, Slides);
  uint8_t ImageLine[SpectraSize];
  for(int Slide=0; Slide<Slides; Slide++)
  { for(int Idx=0; Idx<SpectraSize; Idx++)
    { Float Pwr = (*Power++);
      int Pixel=0;
      if(Pwr>0) Pixel = (int)floor(16+100.0*log10(Pwr/RefPwr)+0.5);
      if(Pixel<0) { Pixel=0; } else if(Pixel>255) { Pixel=255; }
      ImageLine[Idx]=Pixel; }
    fwrite(ImageLine, 1, SpectraSize, ImageFile);
  }
  fclose(ImageFile); return Slides*SpectraSize; }

template <class Float>
 int Spectrogram(SampleBuffer<Float> &SpectraPower, const char *ImageFileName, Float RefPwr=1.00)
{ return Spectrogram(SpectraPower.Data, SpectraPower.Samples(), SpectraPower.Len, ImageFileName, RefPwr); }

// ==================================================================================================

#endif // __BUFFER_H_